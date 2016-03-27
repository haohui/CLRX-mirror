/*
 *  CLRadeonExtender - Unofficial OpenCL Radeon Extensions Library
 *  Copyright (C) 2014-2016 Mateusz Szpakowski
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <CLRX/Config.h>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <climits>
#include <utility>
#include <string>
#include <cassert>
#include <CLRX/amdbin/Elf.h>
#include <CLRX/utils/Utilities.h>
#include <CLRX/utils/MemAccess.h>
#include <CLRX/amdbin/AmdBinaries.h>

static const uint32_t elfMagicValue = 0x464c457fU;

/* INFO: in this file is used ULEV function for conversion
 * from LittleEndian and unaligned access to other memory access policy and endianness
 * Please use this function whenever you want to get or set word in ELF binary,
 * because ELF binaries can be unaligned in memory (as inner binaries).
 */

using namespace CLRX;

/* determine unfinished strings region in string table for checking further consistency */
static size_t unfinishedRegionOfStringTable(const cxbyte* table, size_t size)
{
    if (size == 0) // if zero
        return 0;
    size_t k;
    for (k = size-1; k>0 && table[k]!=0; k--);
    
    return (table[k]==0)?k+1:k;
}

/* elf32 types */

const cxbyte CLRX::Elf32Types::ELFCLASS = ELFCLASS32;
const uint32_t CLRX::Elf32Types::bitness = 32;
const char* CLRX::Elf32Types::bitName = "32";

/* elf64 types */

const cxbyte CLRX::Elf64Types::ELFCLASS = ELFCLASS64;
const cxuint CLRX::Elf64Types::bitness = 64;
const char* CLRX::Elf64Types::bitName = "64";

/* ElfBinaryTemplate */

template<typename Types>
ElfBinaryTemplate<Types>::ElfBinaryTemplate() : binaryCodeSize(0), binaryCode(nullptr),
        sectionStringTable(nullptr), symbolStringTable(nullptr),
        symbolTable(nullptr), dynSymStringTable(nullptr), dynSymTable(nullptr),
        symbolsNum(0), dynSymbolsNum(0),
        symbolEntSize(0), dynSymEntSize(0)
{ }

template<typename Types>
ElfBinaryTemplate<Types>::~ElfBinaryTemplate()
{ }

template<typename Types>
ElfBinaryTemplate<Types>::ElfBinaryTemplate(size_t _binaryCodeSize, cxbyte* _binaryCode,
             Flags _creationFlags) : creationFlags(_creationFlags),
        binaryCodeSize(_binaryCodeSize), binaryCode(_binaryCode),
        sectionStringTable(nullptr), symbolStringTable(nullptr),
        symbolTable(nullptr), dynSymStringTable(nullptr), dynSymTable(nullptr),
        symbolsNum(0), dynSymbolsNum(0), symbolEntSize(0), dynSymEntSize(0)        
{
    if (binaryCodeSize < sizeof(typename Types::Ehdr))
        throw Exception("Binary is too small!!!");
    
    const typename Types::Ehdr* ehdr =
            reinterpret_cast<const typename Types::Ehdr*>(binaryCode);
    
    if (ULEV(*reinterpret_cast<const uint32_t*>(binaryCode)) != elfMagicValue)
        throw Exception("This is not ELF binary");
    if (ehdr->e_ident[EI_CLASS] != Types::ELFCLASS)
        throw Exception(std::string("This is not ")+Types::bitName+"bit ELF binary");
    if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB)
        throw Exception("Other than little-endian binaries are not supported!");
    
    if ((ULEV(ehdr->e_phoff) == 0 && ULEV(ehdr->e_phnum) != 0))
        throw Exception("Elf invalid phoff and phnum combination");
    if (ULEV(ehdr->e_phoff) != 0)
    {   /* reading and checking program headers */
        if (ULEV(ehdr->e_phoff) > binaryCodeSize)
            throw Exception("ProgramHeaders offset out of range!");
        if (usumGt(ULEV(ehdr->e_phoff),
                   ((typename Types::Word)ULEV(ehdr->e_phentsize))*ULEV(ehdr->e_phnum),
                   binaryCodeSize))
            throw Exception("ProgramHeaders offset+size out of range!");
        
        cxuint phnum = ULEV(ehdr->e_phnum);
        // checking program header segment offset ranges
        for (cxuint i = 0; i < phnum; i++)
        {
            const typename Types::Phdr& phdr = getProgramHeader(i);
            if (ULEV(phdr.p_offset) > binaryCodeSize)
                throw Exception("Segment offset out of range!");
            if (usumGt(ULEV(phdr.p_offset), ULEV(phdr.p_filesz), binaryCodeSize))
                throw Exception("Segment offset+size out of range!");
        }
    }
    
    if ((ULEV(ehdr->e_shoff) == 0 && ULEV(ehdr->e_shnum) != 0))
        throw Exception("Elf invalid shoff and shnum combination");
    if (ULEV(ehdr->e_shoff) != 0 && ULEV(ehdr->e_shstrndx) != SHN_UNDEF)
    {   /* indexing of sections */
        if (ULEV(ehdr->e_shoff) > binaryCodeSize)
            throw Exception("SectionHeaders offset out of range!");
        if (usumGt(ULEV(ehdr->e_shoff),
                  ((typename Types::Word)ULEV(ehdr->e_shentsize))*ULEV(ehdr->e_shnum),
                  binaryCodeSize))
            throw Exception("SectionHeaders offset+size out of range!");
        if (ULEV(ehdr->e_shstrndx) >= ULEV(ehdr->e_shnum))
            throw Exception("Shstrndx out of range!");
        
        typename Types::Shdr& shstrShdr = getSectionHeader(ULEV(ehdr->e_shstrndx));
        sectionStringTable = binaryCode + ULEV(shstrShdr.sh_offset);
        const size_t unfinishedShstrPos = unfinishedRegionOfStringTable(
                    sectionStringTable, ULEV(shstrShdr.sh_size));
        
        const typename Types::Shdr* symTableHdr = nullptr;
        const typename Types::Shdr* dynSymTableHdr = nullptr;
        
        cxuint shnum = ULEV(ehdr->e_shnum);
        if ((creationFlags & ELF_CREATE_SECTIONMAP) != 0)
            sectionIndexMap.resize(shnum);
        for (cxuint i = 0; i < shnum; i++)
        {
            const typename Types::Shdr& shdr = getSectionHeader(i);
            /// checking section offset ranges
            if (ULEV(shdr.sh_offset) > binaryCodeSize)
                throw Exception("Section offset out of range!");
            if (ULEV(shdr.sh_type) != SHT_NOBITS)
                if (usumGt(ULEV(shdr.sh_offset), ULEV(shdr.sh_size), binaryCodeSize))
                    throw Exception("Section offset+size out of range!");
            if (ULEV(shdr.sh_link) >= ULEV(ehdr->e_shnum))
                throw Exception("Section link out of range!");
            
            const typename Types::Size sh_nameindx = ULEV(shdr.sh_name);
            if (sh_nameindx >= ULEV(shstrShdr.sh_size))
                throw Exception("Section name index out of range!");
            
            if (sh_nameindx >= unfinishedShstrPos)
                throw Exception("Unfinished section name!");
            
            const char* shname =
                reinterpret_cast<const char*>(sectionStringTable + sh_nameindx);
            
            if ((creationFlags & ELF_CREATE_SECTIONMAP) != 0)
                sectionIndexMap[i] = std::make_pair(shname, i);
            // set symbol table and dynamic symbol table pointers
            if (ULEV(shdr.sh_type) == SHT_SYMTAB)
                symTableHdr = &shdr;
            if (ULEV(shdr.sh_type) == SHT_DYNSYM)
                dynSymTableHdr = &shdr;
        }
        if ((creationFlags & ELF_CREATE_SECTIONMAP) != 0)
            mapSort(sectionIndexMap.begin(), sectionIndexMap.end(), CStringLess());
        
        if (symTableHdr != nullptr)
        {   // indexing symbols
            if (ULEV(symTableHdr->sh_entsize) < sizeof(typename Types::Sym))
                throw Exception("SymTable entry size is too small!");
            
            symbolEntSize = ULEV(symTableHdr->sh_entsize);
            symbolTable = binaryCode + ULEV(symTableHdr->sh_offset);
            if (ULEV(symTableHdr->sh_link) == SHN_UNDEF)
                throw Exception("Symbol table doesn't have string table");
            
            typename Types::Shdr& symstrShdr = getSectionHeader(ULEV(symTableHdr->sh_link));
            symbolStringTable = binaryCode + ULEV(symstrShdr.sh_offset);
            
            const size_t unfinishedSymstrPos = unfinishedRegionOfStringTable(
                    symbolStringTable, ULEV(symstrShdr.sh_size));
            symbolsNum = ULEV(symTableHdr->sh_size)/ULEV(symTableHdr->sh_entsize);
            if ((creationFlags & ELF_CREATE_SYMBOLMAP) != 0)
                symbolIndexMap.resize(symbolsNum);
            
            for (typename Types::Size i = 0; i < symbolsNum; i++)
            {   /* verify symbol names */
                const typename Types::Sym& sym = getSymbol(i);
                const typename Types::Size symnameindx = ULEV(sym.st_name);
                if (symnameindx >= ULEV(symstrShdr.sh_size))
                    throw Exception("Symbol name index out of range!");
                // check whether name is finished in string section content
                if (symnameindx >= unfinishedSymstrPos)
                    throw Exception("Unfinished symbol name!");
                
                const char* symname =
                    reinterpret_cast<const char*>(symbolStringTable + symnameindx);
                // add to symbol map
                if ((creationFlags & ELF_CREATE_SYMBOLMAP) != 0)
                    symbolIndexMap[i] = std::make_pair(symname, i);
            }
            if ((creationFlags & ELF_CREATE_SYMBOLMAP) != 0)
                mapSort(symbolIndexMap.begin(), symbolIndexMap.end(), CStringLess());
        }
        if (dynSymTableHdr != nullptr)
        {   // indexing dynamic symbols
            if (ULEV(dynSymTableHdr->sh_entsize) < sizeof(typename Types::Sym))
                throw Exception("DynSymTable entry size is too small!");
            
            dynSymEntSize = ULEV(dynSymTableHdr->sh_entsize);
            dynSymTable = binaryCode + ULEV(dynSymTableHdr->sh_offset);
            if (ULEV(dynSymTableHdr->sh_link) == SHN_UNDEF)
                throw Exception("DynSymbol table doesn't have string table");
            
            typename Types::Shdr& dynSymstrShdr =
                    getSectionHeader(ULEV(dynSymTableHdr->sh_link));
            dynSymbolsNum = ULEV(dynSymTableHdr->sh_size)/ULEV(dynSymTableHdr->sh_entsize);
            
            dynSymStringTable = binaryCode + ULEV(dynSymstrShdr.sh_offset);
            const size_t unfinishedSymstrPos = unfinishedRegionOfStringTable(
                    dynSymStringTable, ULEV(dynSymstrShdr.sh_size));
            
            if ((creationFlags & ELF_CREATE_DYNSYMMAP) != 0)
                dynSymIndexMap.resize(dynSymbolsNum);
            
            for (typename Types::Size i = 0; i < dynSymbolsNum; i++)
            {   /* verify symbol names */
                const typename Types::Sym& sym = getDynSymbol(i);
                const typename Types::Size symnameindx = ULEV(sym.st_name);
                if (symnameindx >= ULEV(dynSymstrShdr.sh_size))
                    throw Exception("DynSymbol name index out of range!");
                // check whether name is finished in string section content
                if (symnameindx >= unfinishedSymstrPos)
                    throw Exception("Unfinished dynsymbol name!");
                
                const char* symname =
                    reinterpret_cast<const char*>(dynSymStringTable + symnameindx);
                // add to symbol map
                if ((creationFlags & ELF_CREATE_DYNSYMMAP) != 0)
                    dynSymIndexMap[i] = std::make_pair(symname, i);
            }
            if ((creationFlags & ELF_CREATE_DYNSYMMAP) != 0)
                mapSort(dynSymIndexMap.begin(), dynSymIndexMap.end(), CStringLess());
        }
    }
}

template<typename Types>
uint16_t ElfBinaryTemplate<Types>::getSectionIndex(const char* name) const
{
    if (hasSectionMap())
    {
        SectionIndexMap::const_iterator it = binaryMapFind(
                    sectionIndexMap.begin(), sectionIndexMap.end(), name, CStringLess());
        if (it == sectionIndexMap.end())
            throw Exception(std::string("Can't find Elf")+Types::bitName+" Section");
        return it->second;
    }
    else
    {
        for (cxuint i = 0; i < getSectionHeadersNum(); i++)
        {
            if (::strcmp(getSectionName(i), name) == 0)
                return i;
        }
        throw Exception(std::string("Can't find Elf")+Types::bitName+" Section");
    }
}

template<typename Types>
typename Types::Size ElfBinaryTemplate<Types>::getSymbolIndex(const char* name) const
{
    SymbolIndexMap::const_iterator it = binaryMapFind(
                    symbolIndexMap.begin(), symbolIndexMap.end(), name, CStringLess());
    if (it == symbolIndexMap.end())
        throw Exception(std::string("Can't find Elf")+Types::bitName+" Symbol");
    return it->second;
}

template<typename Types>
typename Types::Size ElfBinaryTemplate<Types>::getDynSymbolIndex(const char* name) const
{
    SymbolIndexMap::const_iterator it = binaryMapFind(
                    dynSymIndexMap.begin(), dynSymIndexMap.end(), name, CStringLess());
    if (it == dynSymIndexMap.end())
        throw Exception(std::string("Can't find Elf")+Types::bitName+" DynSymbol");
    return it->second;
}

template class CLRX::ElfBinaryTemplate<CLRX::Elf32Types>;
template class CLRX::ElfBinaryTemplate<CLRX::Elf64Types>;

bool CLRX::isElfBinary(size_t binarySize, const cxbyte* binary)
{
    if (binarySize < sizeof(Elf32_Ehdr) ||
        ULEV(*reinterpret_cast<const uint32_t*>(binary)) != elfMagicValue)
        return false;
    if ((binary[EI_CLASS] != ELFCLASS32 && binary[EI_CLASS] != ELFCLASS64) ||
        binary[EI_DATA] != ELFDATA2LSB) // only LSB elf is supported
        return false;
    if ((binary[EI_CLASS] == ELFCLASS32 && binarySize < sizeof(Elf32_Ehdr)) ||
        (binary[EI_CLASS] == ELFCLASS64 && binarySize < sizeof(Elf64_Ehdr)))
        return false;
    if (ULEV(*((const uint64_t*)(binary+8))) != 0)
        return false;
    return true;
}

/*
 * Elf binary generator
 */

uint16_t CLRX::convertSectionId(cxuint sectionIndex, const uint16_t* builtinSections,
                  cxuint maxBuiltinSection, cxuint extraSectionIndex)
{
    if (sectionIndex == ELFSECTID_NULL)
        return 0;
    if (sectionIndex == ELFSECTID_ABS)
        return SHN_ABS;
    if (sectionIndex == ELFSECTID_UNDEF)
        return SHN_UNDEF;
    if (sectionIndex < ELFSECTID_START)
        return sectionIndex+extraSectionIndex;
    else if (sectionIndex >= ELFSECTID_START && sectionIndex <= maxBuiltinSection)
    {
        const uint16_t shndx = builtinSections[sectionIndex-ELFSECTID_START];
        if (shndx == SHN_UNDEF) // if table entry for sectionIndex is not defined
            throw Exception("Wrong BinSection:sectionId");
        return builtinSections[sectionIndex-ELFSECTID_START];
    }
    else // failed
        throw Exception("Wrong BinSection:sectionId");
}

ElfRegionContent::~ElfRegionContent()
{ }

template<typename Types>
ElfBinaryGenTemplate<Types>::ElfBinaryGenTemplate()
        : sizeComputed(false), addNullSym(true), addNullDynSym(true),
          addNullSection(true), shStrTab(0), strTab(0), dynStr(0), shdrTabRegion(0),
          phdrTabRegion(0)
{ }

template<typename Types>
ElfBinaryGenTemplate<Types>::ElfBinaryGenTemplate(const ElfHeaderTemplate<Types>& _header,
            bool _addNullSym, bool _addNullDynSym, bool _addNullSection)
        : sizeComputed(false), addNullSym(_addNullSym), addNullDynSym(_addNullDynSym),
          addNullSection(_addNullSection), shStrTab(0), strTab(0), dynStr(0),
          shdrTabRegion(0), phdrTabRegion(0), header(_header)
{ }

template<typename Types>
void ElfBinaryGenTemplate<Types>::addRegion(const ElfRegionTemplate<Types>& region)
{ regions.push_back(region); }

template<typename Types>
void ElfBinaryGenTemplate<Types>::addProgramHeader(
            const ElfProgramHeaderTemplate<Types>& progHeader)
{ progHeaders.push_back(progHeader); }

template<typename Types>
void ElfBinaryGenTemplate<Types>::computeSize()
{
    if (sizeComputed) return;
    
    /* verify data */
    if (header.entryRegion != UINT_MAX && header.entryRegion >= regions.size())
        throw Exception("Header entry region out of range");
    
    regionOffsets.reset(new typename Types::Word[regions.size()]);
    size = sizeof(typename Types::Ehdr);
    sectionsNum = addNullSection; // if add null section
    for (const auto& region: regions)
        if (region.type == ElfRegionType::SECTION)
            sectionsNum++;
    sectionRegions.reset(new cxuint[sectionsNum+1]);
    sectionRegions[0] = UINT_MAX;
    cxuint sectionCount = 1;
    
    for (const auto& sym: symbols)
        if (sym.sectionIndex >= sectionsNum)
            throw Exception("Symbol section index out of range");
    for (const auto& sym: dynSymbols)
        if (sym.sectionIndex >= sectionsNum)
            throw Exception("DynSymbol section index out of range");
    
    for (size_t i = 0; i < regions.size(); i++)
    {
        ElfRegionTemplate<Types>& region = regions[i];
        if (region.align > 1)
        {   // fix alignment
            if (region.align!=0 && (size&(region.align-1))!=0)
                size += region.align - (size&(region.align-1));
        }
        
        regionOffsets[i] = size;
        // add region size
        if (region.type == ElfRegionType::PHDR_TABLE)
        {
            size += uint64_t(progHeaders.size())*sizeof(typename Types::Phdr);
            region.size = size-regionOffsets[i];
            phdrTabRegion = i;
            for (const auto& progHdr: progHeaders)
            {
                if (progHdr.regionStart >= regions.size())
                    throw Exception("Region start out of range");
                if (uint64_t(progHdr.regionStart) + progHdr.regionsNum > regions.size())
                    throw Exception("Region end out of range");
            }
        }
        else if (region.type == ElfRegionType::SHDR_TABLE)
        {
            size += uint64_t(sectionsNum)*sizeof(typename Types::Shdr);
            region.size = size-regionOffsets[i];
            shdrTabRegion = i;
        }
        else if (region.type == ElfRegionType::USER)
            size += region.size;
        else if (region.type == ElfRegionType::SECTION)
        {   // if section
            if (region.section.link >= sectionsNum)
                throw Exception("Section link out of range");
            
            if (region.section.type != SHT_NOBITS && region.size != 0)
                size += region.size;
            else // otherwise get default size for symtab, dynsym, strtab, dynstr
            {
                if (region.section.type == SHT_SYMTAB)
                    size += uint64_t(symbols.size()+addNullSym)*
                                sizeof(typename Types::Sym);
                else if (region.section.type == SHT_DYNSYM)
                    size += uint64_t(dynSymbols.size()+addNullDynSym)*
                                sizeof(typename Types::Sym);
                else if (region.section.type == SHT_STRTAB)
                {
                    if (::strcmp(region.section.name, ".strtab") == 0)
                    {
                        size += (addNullSym);
                        for (const auto& sym: symbols)
                            if (sym.name != nullptr && sym.name[0] != 0)
                                size += ::strlen(sym.name)+1;
                    }
                    else if (::strcmp(region.section.name, ".dynstr") == 0)
                    {
                        size += (addNullDynSym);
                        for (const auto& sym: dynSymbols)
                            if (sym.name != nullptr && sym.name[0] != 0)
                                size += ::strlen(sym.name)+1;
                    }
                    else if (::strcmp(region.section.name, ".shstrtab") == 0)
                    {
                        size += (addNullSection);
                        for (const auto& region2: regions)
                        {
                            if (region2.type == ElfRegionType::SECTION &&
                                region2.section.name != nullptr &&
                                region2.section.name[0] != 0)
                                size += ::strlen(region2.section.name)+1;
                        }
                    }
                }
                if (region.section.type != SHT_NOBITS)
                    region.size = size-regionOffsets[i];
            }
            if (::strcmp(region.section.name, ".strtab") == 0)
                strTab = sectionCount;
            else if (::strcmp(region.section.name, ".dynstr") == 0)
                dynStr = sectionCount;
            else if (::strcmp(region.section.name, ".shstrtab") == 0)
                shStrTab = sectionCount;
            sectionRegions[sectionCount] = i;
            sectionCount++;
        }
    }
    
    sizeComputed = true;
}

template<typename Types>
uint64_t ElfBinaryGenTemplate<Types>::countSize()
{
    computeSize();
    return size;
}

template<typename Types>
void ElfBinaryGenTemplate<Types>::generate(FastOutputBuffer& fob)
{
    computeSize();
    const uint64_t startOffset = fob.getWritten();
    /* write elf header */
    {
        typename Types::Ehdr ehdr;
        ::memset(ehdr.e_ident, 0, EI_NIDENT);
        ehdr.e_ident[0] = 0x7f;
        ehdr.e_ident[1] = 'E';
        ehdr.e_ident[2] = 'L';
        ehdr.e_ident[3] = 'F';
        ehdr.e_ident[4] = Types::ELFCLASS;
        ehdr.e_ident[5] = ELFDATA2LSB;
        ehdr.e_ident[6] = EV_CURRENT;
        ehdr.e_ident[EI_OSABI] = header.osABI;
        ehdr.e_ident[EI_ABIVERSION] = header.abiVersion;
        SLEV(ehdr.e_type, header.type);
        SLEV(ehdr.e_machine, header.machine);
        SLEV(ehdr.e_version, header.version);
        SLEV(ehdr.e_flags, header.flags);
        if (header.entryRegion != UINT_MAX)
        {   // if have entry
            typename Types::Word entry = regionOffsets[header.entryRegion] + header.entry;
            if (regions[header.entryRegion].type == ElfRegionType::SECTION &&
                regions[header.entryRegion].section.addrBase != 0)
                entry += regions[header.entryRegion].section.addrBase;
            else
                entry += header.vaddrBase;
            
            SLEV(ehdr.e_entry, entry);
        }
        else
            SLEV(ehdr.e_entry, 0);
        SLEV(ehdr.e_ehsize, sizeof(typename Types::Ehdr));
        if (!progHeaders.empty())
        {
            SLEV(ehdr.e_phentsize, sizeof(typename Types::Phdr));
            SLEV(ehdr.e_phoff, regionOffsets[phdrTabRegion]);
        }
        else
        {
            SLEV(ehdr.e_phentsize, 0);
            SLEV(ehdr.e_phoff, 0);
        }
        SLEV(ehdr.e_phnum, progHeaders.size());
        SLEV(ehdr.e_shentsize, sizeof(typename Types::Shdr));
        SLEV(ehdr.e_shnum, sectionsNum);
        SLEV(ehdr.e_shoff, regionOffsets[shdrTabRegion]);
        SLEV(ehdr.e_shstrndx, shStrTab);
        
        fob.writeObject(ehdr);
    }
    
    size_t nullSymNameOffset = 0;
    // if addNullSym is not set, then no empty symbol name added, then we
    // find first null character
    if (!addNullSym && !symbols.empty())
        nullSymNameOffset = ::strlen(symbols[0].name);
    size_t nullDynSymNameOffset = 0;
    // if addNullDynSym is not set, then no empty dynamic symbol name added, then we
    // find first null character
    if (!addNullDynSym && !dynSymbols.empty())
        nullDynSymNameOffset = ::strlen(dynSymbols[0].name);
    // if addNullSection is not set, then no empty section name added, then we
    // find first null character
    size_t nullSectionNameOffset = 0;
    if (!addNullSection)
    {
        for (const ElfRegionTemplate<Types>& reg: regions)
            if (reg.type == ElfRegionType::SECTION)
            {
                nullSectionNameOffset = ::strlen(reg.section.name);
                break;
            }
    }
    
    /* write regions */
    for (size_t i = 0; i < regions.size(); i++)
    {   
        const ElfRegionTemplate<Types>& region = regions[i];
        // fix alignment
        uint64_t toFill = 0;
        if (region.align > 1)
        {
            const uint64_t curOffset = (fob.getWritten()-startOffset);
            if (region.align!=0 && (curOffset&(region.align-1))!=0)
                toFill = region.align - (curOffset&(region.align-1));
            fob.fill(toFill, 0);
        }
        assert(regionOffsets[i] == fob.getWritten()-startOffset);
        
        // write content
        if (region.type == ElfRegionType::PHDR_TABLE)
        {   /* write program headers */
            for (const auto& progHeader: progHeaders)
            {
                typename Types::Phdr phdr;
                SLEV(phdr.p_type, progHeader.type);
                SLEV(phdr.p_flags, progHeader.flags);
                const ElfRegionTemplate<Types>& sregion = regions[progHeader.regionStart];
                bool zeroOffset = sregion.type == ElfRegionType::SECTION &&
                        sregion.section.zeroOffset;
                SLEV(phdr.p_offset, !zeroOffset ?
                        regionOffsets[progHeader.regionStart] : 0);
                SLEV(phdr.p_align, regions[progHeader.regionStart].align);
                
                /* paddrBase and vaddrBase is base to program header virtual and physical
                 * addresses for program header. if not defined then get address base
                 * from ELF header */
                if (progHeader.offsetAsPAddress)
                    SLEV(phdr.p_paddr, regionOffsets[progHeader.regionStart]);
                else if (progHeader.paddrBase != 0)
                    SLEV(phdr.p_paddr, progHeader.paddrBase +
                                regionOffsets[progHeader.regionStart]);
                else if (header.paddrBase != 0)
                    SLEV(phdr.p_paddr, header.paddrBase +
                                regionOffsets[progHeader.regionStart]);
                else
                    SLEV(phdr.p_paddr, 0);
                
                if (progHeader.offsetAsVAddress)
                    SLEV(phdr.p_vaddr, regionOffsets[progHeader.regionStart]);
                else if (progHeader.vaddrBase != 0)
                    SLEV(phdr.p_vaddr, progHeader.vaddrBase +
                                regionOffsets[progHeader.regionStart]);
                else if (header.vaddrBase != 0)
                    SLEV(phdr.p_vaddr, header.vaddrBase +
                                regionOffsets[progHeader.regionStart]);
                else
                    SLEV(phdr.p_vaddr, 0);
                
                /// fileSize - add offset of first region to simulate region alignment
                typename Types::Word fileSize = regionOffsets[progHeader.regionStart];
                typename Types::Word phSize = 0;
                for (cxuint k = progHeader.regionStart; k < progHeader.regionStart+
                            progHeader.regionsNum; k++)
                {
                    size_t toFill = 0;
                    if (regions[k].type != ElfRegionType::SECTION ||
                                regions[k].section.type != SHT_NOBITS)
                    {   // add alignment
                        if (regions[k].align!=0 && (fileSize&(regions[k].align-1))!=0)
                            toFill = regions[k].align - (fileSize&(regions[k].align-1));
                        fileSize += regions[k].size + toFill;
                    }
                    // add alignment to size
                    toFill = 0;
                    if (regions[k].align!=0 && (phSize&(regions[k].align-1))!=0)
                        toFill = regions[k].align - (phSize&(regions[k].align-1));
                    phSize += regions[k].size + toFill;
                }
                // first region offset is obsolete, just subtract
                fileSize -= regionOffsets[progHeader.regionStart];
                SLEV(phdr.p_filesz, phSize);
                
                if (progHeader.haveMemSize)
                {
                    if (progHeader.memSize != 0)
                        SLEV(phdr.p_memsz, progHeader.memSize);
                    else
                        SLEV(phdr.p_memsz, phSize);
                }
                else
                    SLEV(phdr.p_memsz, 0);
                SLEV(phdr.p_filesz, fileSize);
                fob.writeObject(phdr);
            }
        }
        else if (region.type == ElfRegionType::SHDR_TABLE)
        {   /* write section headers table */
            if (addNullSection)
                fob.fill(sizeof(typename Types::Shdr), 0);
            uint32_t nameOffset = (addNullSection);
            for (cxuint j = 0; j < regions.size(); j++)
            {
                const auto& region2 = regions[j];
                if (region2.type == ElfRegionType::SECTION)
                {
                    typename Types::Shdr shdr;
                    if (region2.section.name!=nullptr && region2.section.name[0]!=0)
                        SLEV(shdr.sh_name, nameOffset);
                    else // set empty name offset
                        SLEV(shdr.sh_name, nullSectionNameOffset);
                    SLEV(shdr.sh_type, region2.section.type);
                    SLEV(shdr.sh_flags, region2.section.flags);
                    SLEV(shdr.sh_offset, (!region2.section.zeroOffset) ?
                                regionOffsets[j] : 0);
                    /* addrBase is base address of first section. if not defined
                     * use address base as virtual address base from elf header */
                    if (region2.section.offsetAsAddress)
                        SLEV(shdr.sh_addr, regionOffsets[j]);
                    else if (region2.section.addrBase != 0)
                        SLEV(shdr.sh_addr, region2.section.addrBase+regionOffsets[j]);
                    else if (header.vaddrBase != 0)
                        SLEV(shdr.sh_addr, header.vaddrBase+regionOffsets[j]);
                    else
                        SLEV(shdr.sh_addr, 0);
                    
                    if (region2.align != 0 || j+1 >= regions.size() ||
                        regionOffsets[j]+region2.size == regionOffsets[j+1])
                        SLEV(shdr.sh_size, region2.size);
                    else
                        SLEV(shdr.sh_size, regionOffsets[j+1]-regionOffsets[j]);
                    SLEV(shdr.sh_info, region2.section.info);
                    SLEV(shdr.sh_addralign, region2.align);
                    if (region2.section.link == 0)
                    {
                        if (::strcmp(region2.section.name, ".symtab") == 0)
                            SLEV(shdr.sh_link, strTab);
                        else if (::strcmp(region2.section.name, ".dynsym") == 0)
                            SLEV(shdr.sh_link, dynStr);
                        else
                            SLEV(shdr.sh_link, region2.section.link);
                    }
                    else
                        SLEV(shdr.sh_link, region2.section.link);
                    
                    if (region2.section.type == SHT_SYMTAB ||
                        region2.section.type == SHT_DYNSYM)
                        SLEV(shdr.sh_entsize, sizeof(typename Types::Sym));
                    else
                        SLEV(shdr.sh_entsize, region2.section.entSize);
                    if (region2.section.name!=nullptr && region2.section.name[0]!=0)
                        nameOffset += ::strlen(region2.section.name)+1;
                    fob.writeObject(shdr);
                }
            }
        }
        else if (region.type == ElfRegionType::USER)
        {
            if (region.dataFromPointer)
                fob.writeArray(region.size, region.data);
            else
                (*region.dataGen)(fob);
        }
        else if (region.type == ElfRegionType::SECTION)
        {
            if (region.data == nullptr)
            {
                if (region.section.type == SHT_SYMTAB || region.section.type == SHT_DYNSYM)
                {
                    uint32_t nameOffset = 0;
                    if (region.section.type == SHT_SYMTAB && addNullSym)
                    {
                        fob.fill(sizeof(typename Types::Sym), 0);
                        nameOffset = 1;
                    }
                    if (region.section.type == SHT_DYNSYM && addNullDynSym)
                    {
                        fob.fill(sizeof(typename Types::Sym), 0);
                        nameOffset = 1;
                    }
                    const auto& symbolsList = (region.section.type == SHT_SYMTAB) ?
                            symbols : dynSymbols;
                    for (const auto& inSym: symbolsList)
                    {
                        typename Types::Sym sym;
                        if (inSym.name != nullptr && inSym.name[0] != 0)
                            SLEV(sym.st_name, nameOffset);
                        else  // set empty name offset (symbol or dynamic symbol)
                            SLEV(sym.st_name, (region.section.type == SHT_SYMTAB) ?
                                        nullSymNameOffset : nullDynSymNameOffset);
                        
                        SLEV(sym.st_shndx, inSym.sectionIndex);
                        SLEV(sym.st_size, inSym.size);
                        /// if value defined as address
                        if (!inSym.valueIsAddr)
                            SLEV(sym.st_value, inSym.value);
                        // if not use conversion to address with section addrBase
                        else if (inSym.sectionIndex != 0 && regions[sectionRegions[
                                    inSym.sectionIndex]].section.addrBase != 0)
                            SLEV(sym.st_value, inSym.value + regionOffsets[
                                    sectionRegions[inSym.sectionIndex]] +
                                    regions[sectionRegions[inSym.sectionIndex]].
                                            section.addrBase);
                        else // use elf headerf virtual address base
                            SLEV(sym.st_value, inSym.value + regionOffsets[
                                sectionRegions[inSym.sectionIndex]] + header.vaddrBase);
                        sym.st_other = inSym.other;
                        sym.st_info = inSym.info;
                        if (inSym.name != nullptr && inSym.name[0] != 0)
                            nameOffset += ::strlen(inSym.name)+1;
                        fob.writeObject(sym);
                    }
                }
                else if (region.section.type == SHT_STRTAB)
                {
                    if (::strcmp(region.section.name, ".strtab") == 0)
                    {
                        if (addNullSym)
                            fob.put(0);
                        for (const auto& sym: symbols)
                            if (sym.name != nullptr && sym.name[0] != 0)
                                fob.write(::strlen(sym.name)+1, sym.name);
                    }
                    else if (::strcmp(region.section.name, ".dynstr") == 0)
                    {
                        if (addNullDynSym)
                            fob.put(0);
                        for (const auto& sym: dynSymbols)
                            if (sym.name != nullptr && sym.name[0] != 0)
                                fob.write(::strlen(sym.name)+1, sym.name);
                    }
                    else if (::strcmp(region.section.name, ".shstrtab") == 0)
                    {
                        if (addNullSection)
                            fob.put(0);
                        for (const auto& region2: regions)
                            if (region2.type == ElfRegionType::SECTION &&
                                region2.section.name != nullptr &&
                                region2.section.name[0] != 0)
                                fob.write(::strlen(region2.section.name)+1,
                                          region2.section.name);
                    }
                }
            }
            else if (region.section.type != SHT_NOBITS)
            {
                if (region.dataFromPointer)
                    fob.writeArray(region.size, region.data);
                else
                    (*region.dataGen)(fob);
            }
        }
    }
    fob.flush();
    fob.getOStream().flush();
    assert(size == fob.getWritten()-startOffset);
}

template class CLRX::ElfBinaryGenTemplate<CLRX::Elf32Types>;
template class CLRX::ElfBinaryGenTemplate<CLRX::Elf64Types>;
