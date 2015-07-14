/*
 *  CLRadeonExtender - Unofficial OpenCL Radeon Extensions Library
 *  Copyright (C) 2014-2015 Mateusz Szpakowski
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
/*! \file Assembler.h
 * \brief an assembler for Radeon GPU's
 */

#ifndef __CLRX_ASSEMBLER_H__
#define __CLRX_ASSEMBLER_H__

#include <CLRX/Config.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <istream>
#include <ostream>
#include <iostream>
#include <vector>
#include <utility>
#include <stack>
#include <unordered_set>
#include <unordered_map>
#include <CLRX/amdbin/AmdBinaries.h>
#include <CLRX/amdbin/GalliumBinaries.h>
#include <CLRX/amdbin/AmdBinGen.h>
#include <CLRX/utils/Utilities.h>
#include <CLRX/amdasm/Commons.h>
#include <CLRX/amdasm/AsmSource.h>

/// main namespace
namespace CLRX
{

enum: cxuint
{
    ASM_WARNINGS = 1,   ///< enable all warnings for assembler
    ASM_64BIT_MODE = 2, ///< assemble to 64-bit addressing mode
    ASM_GNU_AS_COMPAT = 4, ///< compatibility with GNU as (expressions)
    ASM_ALL = 0xff  ///< all flags
};

enum: cxuint
{
    ASMSECT_ABS = UINT_MAX,  ///< absolute section id
    ASMKERN_GLOBAL = UINT_MAX, ///< no kernel, global space
};

/// assembler section type
enum class AsmSectionType: cxbyte
{
    DATA = 0,       ///< kernel or global data
    CODE,           ///< code of program or kernel
    CONFIG,         ///< configuration (global or for kernel)
    LAST_COMMON = CONFIG,   ///< last common type
    
    AMD_HEADER = LAST_COMMON+1, ///< AMD Catalyst kernel's header
    AMD_METADATA,       ///< AMD Catalyst kernel's metadata
    AMD_LLVMIR,         ///< LLVMIR for AMD binary
    AMD_SOURCE,         ///< AMD source code
    
    GALLIUM_COMMENT = LAST_COMMON+1,    ///< gallium comment section
    GALLIUM_DISASM,         ///< disassembly section
    CUSTOM = 0xff
};

class Assembler;

enum
{
    ASMSECT_WRITEABLE = 1,
    ASMSECT_ABS_ADDRESSABLE = 2
};

/// assdembler format handler
class AsmFormatHandler
{
protected:
    GPUDeviceType deviceType;
    bool _64Bit;
    
    cxuint kernel;
    cxuint section;
    
    AsmFormatHandler(GPUDeviceType deviceType, bool is64Bit);
public:
    virtual ~AsmFormatHandler();
    
    /// set current kernel by name
    virtual cxuint addKernel(const char* kernelName) = 0;
    /// set current section by name
    virtual cxuint addSection(const char* sectionName, cxuint kernelId,
                      AsmSectionType type) = 0;
    
    void setKernel(cxuint kernel)
    { this->kernel = kernel; }
    
    void setSection(cxuint section)
    { this->section = section; }
    
    /// set data for current section
    virtual void setSectionData(cxuint sectionId, size_t contentSize,
                        const cxbyte* content) = 0;
    // get current section flags
    virtual cxuint getSectionFlags(cxuint sectionId) = 0;
    /// parse pseudo-op
    virtual bool parsePseudoOp(const char* string) = 0;
    /// write binaery to output stream
    virtual void writeBinary(std::ostream& os) = 0;
};

/// handles raw code format
class AsmRawCodeHandler: public AsmFormatHandler
{
private:
    size_t contentSize;
    const cxbyte* content;
public:
    AsmRawCodeHandler(GPUDeviceType deviceType, bool is64Bit);
    ~AsmRawCodeHandler();
    
    /// set current kernel by name
    cxuint addKernel(const char* kernelName);
    /// set current section by name
    cxuint addSection(const char* sectionName, cxuint kernelId, AsmSectionType type);
    
    /// set data for current section
    void setSectionData(cxuint sectionId, size_t contentSize, const cxbyte* content);
    // get current section flags
    cxuint getSectionFlags(cxuint sectionId);
    /// parse pseudo-op
    bool parsePseudoOp(const char* string);
    /// write binaery to output stream
    void writeBinary(std::ostream& os);
};

/// handles AMD Catalyst format
class AsmAmdHandler: public AsmFormatHandler
{
private:
    AmdInput input;
public:
    AsmAmdHandler(GPUDeviceType deviceType, bool is64Bit);
    ~AsmAmdHandler();
    
    /// set current kernel by name
    cxuint addKernel(const char* kernelName);
    /// set current section by name
    cxuint addSection(const char* sectionName, cxuint kernelId, AsmSectionType type);
    
    /// set data for current section
    void setSectionData(cxuint sectionId, size_t contentSize, const cxbyte* content);
    // get current section flags
    cxuint getSectionFlags(cxuint sectionId);
    /// parse pseudo-op
    bool parsePseudoOp(const char* string);
    /// write binaery to output stream
    void writeBinary(std::ostream& os);
};

/// handles GalliumCompute format
class AsmGalliumHandler: public AsmFormatHandler
{
private:
    GalliumInput input;
public:
    AsmGalliumHandler(GPUDeviceType deviceType, bool is64Bit);
    ~AsmGalliumHandler();
    
    /// set current kernel by name
    cxuint addKernel(const char* kernelName);
    /// set current section by name
    cxuint addSection(const char* sectionName, cxuint kernelId, AsmSectionType type);
    
    /// set data for current section
    void setSectionData(cxuint sectionId, size_t contentSize, const cxbyte* content);
    // get current section flags
    cxuint getSectionFlags(cxuint sectionId);
    /// parse pseudo-op
    bool parsePseudoOp(const char* string);
    /// write binaery to output stream
    void writeBinary(std::ostream& os);
};

/// ISA assembler class
class ISAAssembler
{
protected:
    Assembler& assembler;       ///< assembler
    
    /// constructor
    explicit ISAAssembler(Assembler& assembler);
public:
    /// destructor
    virtual ~ISAAssembler();
    
    /// assemble single line
    virtual size_t assemble(uint64_t lineNo, const char* line,
                std::vector<cxbyte>& output) = 0;
    /// resolve code with location, target and value
    virtual bool resolveCode(cxbyte* location, cxbyte targetType, uint64_t value) = 0;
    /// check if name is mnemonic
    virtual bool checkMnemonic(const std::string& mnemonic) = 0;
};

/// GCN arch assembler
class GCNAssembler: public ISAAssembler
{
public:
    /// constructor
    explicit GCNAssembler(Assembler& assembler);
    /// destructor
    ~GCNAssembler();
    
    /// assemble single line
    size_t assemble(uint64_t lineNo, const char* line, std::vector<cxbyte>& output);
    /// resolve code with location, target and value
    bool resolveCode(cxbyte* location, cxbyte targetType, uint64_t value);
    /// check if name is mnemonic
    bool checkMnemonic(const std::string& mnemonic);
};

/*
 * assembler expressions
 */

/// assembler expression operator
enum class AsmExprOp : cxbyte
{
    ARG_VALUE = 0,  ///< absolute value
    ARG_SYMBOL = 1,  ///< absolute symbol without defined value
    NEGATE = 2, ///< negation
    BIT_NOT,    ///< binary negation
    LOGICAL_NOT,    ///< logical negation
    PLUS,   ///< plus (nothing)
    ADDITION,   ///< addition
    SUBTRACT,       ///< subtraction
    MULTIPLY,       ///< multiplication
    DIVISION,       ///< unsigned division
    SIGNED_DIVISION,    ///< signed division
    MODULO,         ///< unsigned modulo
    SIGNED_MODULO,      ///< signed modulo
    BIT_AND,        ///< bitwise AND
    BIT_OR,         //< bitwise OR
    BIT_XOR,        ///< bitwise XOR
    BIT_ORNOT,      ///< bitwise OR-not
    SHIFT_LEFT,     ///< shift left
    SHIFT_RIGHT,    ///< logical shift irght
    SIGNED_SHIFT_RIGHT, ///< signed (arithmetic) shift right
    LOGICAL_AND,    ///< logical AND
    LOGICAL_OR,     ///< logical OR
    EQUAL,          ///< equality
    NOT_EQUAL,      ///< unequality
    LESS,           ///< less than
    LESS_EQ,        ///< less or equal than
    GREATER,        ///< greater than
    GREATER_EQ,     ///< greater or equal than
    BELOW, ///< unsigned less
    BELOW_EQ, ///< unsigned less or equal
    ABOVE, ///< unsigned less
    ABOVE_EQ, ///< unsigned less or equal
    CHOICE,  ///< a ? b : c
    CHOICE_START,   ///< helper
    FIRST_ARG = ARG_VALUE,
    LAST_ARG = ARG_SYMBOL,
    FIRST_UNARY = NEGATE,   ///< helper
    LAST_UNARY = PLUS,  ///< helper
    FIRST_BINARY = ADDITION,    ///< helper
    LAST_BINARY = ABOVE_EQ, ///< helper
    NONE = 0xff ///< none operation
};

struct AsmExprTarget;

enum : cxbyte
{
    ASMXTGT_SYMBOL = 0, ///< target is symbol
    ASMXTGT_DATA8,      ///< target is byte
    ASMXTGT_DATA16,     ///< target is 16-bit word
    ASMXTGT_DATA32,     ///< target is 32-bit word
    ASMXTGT_DATA64      ///< target is 64-bit word
};

union AsmExprArg;

class AsmExpression;

/// assembler symbol occurrence in expression
struct AsmExprSymbolOccurrence
{
    AsmExpression* expression;      ///< target expression pointer
    size_t argIndex;        ///< argument index
    size_t opIndex;         ///< operator index
    
    /// comparison operator
    bool operator==(const AsmExprSymbolOccurrence& b) const
    { return expression==b.expression && opIndex==b.opIndex && argIndex==b.argIndex; }
};

/// assembler symbol structure
struct AsmSymbol
{
    cxuint refCount;    ///< reference counter (for internal use only)
    cxuint sectionId;       ///< section id
    cxbyte info;           ///< ELF symbol info
    cxbyte other;           ///< ELF symbol other
    cxuint hasValue:1;         ///< symbol is defined
    cxuint onceDefined:1;       ///< symbol can be only once defined (likes labels)
    cxuint resolving:1;         ///< helper
    cxuint base:1;              ///< with base expression
    cxuint snapshot:1;          ///< if symbol is snapshot
    uint64_t value;         ///< value of symbol
    uint64_t size;          ///< size of symbol
    AsmExpression* expression;      ///< expression of symbol (if not resolved)
    
    /** list of occurrences in expressions */
    std::vector<AsmExprSymbolOccurrence> occurrencesInExprs;
    
    /// empty constructor
    explicit AsmSymbol(bool _onceDefined = false) :
            refCount(1), sectionId(ASMSECT_ABS), info(0), other(0), hasValue(false),
            onceDefined(_onceDefined), resolving(false), base(false), snapshot(false),
            value(0), size(0), expression(nullptr)
    { }
    /// constructor with expression
    explicit AsmSymbol(AsmExpression* expr, bool _onceDefined = false, bool _base = false) :
            refCount(1), sectionId(ASMSECT_ABS), info(0), other(0), hasValue(false),
            onceDefined(_onceDefined), resolving(false), base(_base),
            snapshot(false), value(0), size(0), expression(expr)
    { }
    /// constructor with value and section id
    explicit AsmSymbol(cxuint _sectionId, uint64_t _value, bool _onceDefined = false) :
            refCount(1), sectionId(_sectionId), info(0), other(0), hasValue(true),
            onceDefined(_onceDefined), resolving(false), base(false), snapshot(false),
            value(_value), size(0), expression(nullptr)
    { }
    /// destructor
    ~AsmSymbol();
    
    /// adds occurrence in expression
    void addOccurrenceInExpr(AsmExpression* expr, size_t argIndex, size_t opIndex)
    { occurrencesInExprs.push_back({expr, argIndex, opIndex}); }
    /// remove occurrence in expression
    void removeOccurrenceInExpr(AsmExpression* expr, size_t argIndex, size_t opIndex);
    /// clear list of occurrences in expression
    void clearOccurrencesInExpr();
    /// make symbol as undefined
    void undefine();
};

/// assembler symbol map
typedef std::unordered_map<std::string, AsmSymbol> AsmSymbolMap;
/// assembler symbol entry
typedef AsmSymbolMap::value_type AsmSymbolEntry;

/// target for assembler expression
struct AsmExprTarget
{
    cxbyte type;    ///< type of target
    union
    {
        AsmSymbolEntry* symbol; ///< symbol entry (if ASMXTGT_SYMBOL)
        struct {
            cxuint sectionId;   ///< section id of destination
            size_t offset;      ///< offset of destination
        };
    };
    /// make symbol target for expression
    static AsmExprTarget symbolTarget(AsmSymbolEntry* entry)
    { 
        AsmExprTarget target;
        target.type = ASMXTGT_SYMBOL;
        target.symbol = entry;
        return target;
    }
    
    /// make n-bit word target for expression
    template<typename T>
    static AsmExprTarget dataTarget(cxuint sectionId, size_t offset)
    {
        AsmExprTarget target;
        target.type = (sizeof(T)==1) ? ASMXTGT_DATA8 : (sizeof(T)==2) ? ASMXTGT_DATA16 :
                (sizeof(T)==4) ? ASMXTGT_DATA32 : ASMXTGT_DATA64;
        target.sectionId = sectionId;
        target.offset = offset;
        return target;
    }
};

/// assembler expression class
class AsmExpression
{
private:
    class TempSymbolSnapshotMap;
    
    AsmExprTarget target;
    AsmSourcePos sourcePos;
    size_t symOccursNum;
    bool relativeSymOccurs;
    bool baseExpr;
    Array<AsmExprOp> ops;
    std::unique_ptr<LineCol[]> messagePositions;    ///< for every potential message
    std::unique_ptr<AsmExprArg[]> args;
    
    AsmSourcePos getSourcePos(size_t msgPosIndex) const
    {
        AsmSourcePos pos = sourcePos;
        pos.lineNo = messagePositions[msgPosIndex].lineNo;
        pos.colNo = messagePositions[msgPosIndex].colNo;
        return pos;
    }
    
    static bool makeSymbolSnapshot(Assembler& assembler,
               TempSymbolSnapshotMap* snapshotMap, const AsmSymbolEntry& symEntry,
               AsmSymbolEntry*& outSymEntry, const AsmSourcePos* topParentSourcePos);
    
    AsmExpression();
    void setParams(size_t symOccursNum, bool relativeSymOccurs,
            size_t _opsNum, const AsmExprOp* ops, size_t opPosNum, const LineCol* opPos,
            size_t argsNum, const AsmExprArg* args, bool baseExpr = false);
public:
    /// constructor of expression (helper)
    AsmExpression(const AsmSourcePos& pos, size_t symOccursNum, bool relativeSymOccurs,
            size_t opsNum, size_t opPosNum, size_t argsNum, bool baseExpr = false);
    /// constructor of expression (helper)
    AsmExpression(const AsmSourcePos& pos, size_t symOccursNum, bool relativeSymOccurs,
              size_t opsNum, const AsmExprOp* ops, size_t opPosNum,
              const LineCol* opPos, size_t argsNum, const AsmExprArg* args,
              bool baseExpr = false);
    /// destructor
    ~AsmExpression();
    
    /// return true if expression is empty
    bool isEmpty() const
    { return ops.empty(); }

    /// helper to create symbol snapshot. Creates initial expression for symbol snapshot
    AsmExpression* createForSnapshot(const AsmSourcePos* exprSourcePos) const;
    
    /// set target of expression
    void setTarget(AsmExprTarget _target)
    { target = _target; }
    
    /// try to evaluate expression
    /**
     * \param assembler assembler instace
     * \param value output value
     * \param sectionId output section id
     * \return true if evaluated
     */
    bool evaluate(Assembler& assembler, uint64_t& value, cxuint& sectionId) const;
    
    /// parse expression. By default, also gets values of symbol or  creates them
    /** parse expresion from assembler's line string. Accepts empty expression.
     * \param assembler assembler
     * \param linePos position in line
     * \param outLinePos position in line after parsing
     * \param makeBase do not evaluate resolved symbols, put them to expression
     * \param dontReolveSymbolsLater do not resolve symbols later
     * \return expression pointer
     */
    static AsmExpression* parse(Assembler& assembler, size_t linePos, size_t& outLinePos,
                    bool makeBase = false, bool dontReolveSymbolsLater = false);
    
    /// parse expression. By default, also gets values of symbol or  creates them
    /** parse expresion from assembler's line string. Accepts empty expression.
     * \param assembler assembler
     * \param linePlace string at position in line
     * \param outend string at position in line after parsing
     * \param makeBase do not evaluate resolved symbols, put them to expression
     * \param dontReolveSymbolsLater do not resolve symbols later
     * \return expression pointer
     */
    static AsmExpression* parse(Assembler& assembler, const char* linePlace,
              const char*& outend, bool makeBase = false,
              bool dontReolveSymbolsLater = false);
    
    /// return true if is argument op
    static bool isArg(AsmExprOp op)
    { return (AsmExprOp::FIRST_ARG <= op && op <= AsmExprOp::LAST_ARG); }
    /// return true if is unary op
    static bool isUnaryOp(AsmExprOp op)
    { return (AsmExprOp::FIRST_UNARY <= op && op <= AsmExprOp::LAST_UNARY); }
    /// return true if is binary op
    static bool isBinaryOp(AsmExprOp op)
    { return (AsmExprOp::FIRST_BINARY <= op && op <= AsmExprOp::LAST_BINARY); }
    /// get targer of expression
    const AsmExprTarget& getTarget() const
    { return target; }
    /// get number of symbol occurrences in expression
    size_t getSymOccursNum() const
    { return symOccursNum; }
    /// get number of symbol occurrences in expression
    size_t hasRelativeSymOccurs() const
    { return relativeSymOccurs; }
    /// unreference symbol occurrences in expression (used internally)
    bool unrefSymOccursNum()
    { return --symOccursNum!=0; }
    
    /// substitute occurrence in expression by value
    void substituteOccurrence(AsmExprSymbolOccurrence occurrence, uint64_t value,
                  cxuint sectionId = ASMSECT_ABS);
    /// get operators list
    const Array<AsmExprOp>& getOps() const
    { return ops; }
    /// get argument list
    const AsmExprArg* getArgs() const
    { return args.get(); }
    /// get source position
    const AsmSourcePos& getSourcePos() const
    { return sourcePos; }

    /// make symbol snapshot (required to implement .eqv pseudo-op)    
    static bool makeSymbolSnapshot(Assembler& assembler, const AsmSymbolEntry& symEntry,
               AsmSymbolEntry*& outSymEntry, const AsmSourcePos* parentExprSourcePos);
};

inline AsmSymbol::~AsmSymbol()
{
    if (base) delete expression; // if symbol with base expresion
    clearOccurrencesInExpr();
}

/// assembler expression argument
union AsmExprArg
{
    AsmSymbolEntry* symbol; ///< if symbol
    uint64_t value;         ///< value
    struct {
        uint64_t value;         ///< value
        cxuint sectionId;       ///< sectionId
    } relValue; ///< relative value (with section)
};

inline void AsmExpression::substituteOccurrence(AsmExprSymbolOccurrence occurrence,
                        uint64_t value, cxuint sectionId)
{
    ops[occurrence.opIndex] = AsmExprOp::ARG_VALUE;
    args[occurrence.argIndex].relValue.value = value;
    args[occurrence.argIndex].relValue.sectionId = sectionId;
    if (sectionId != ASMSECT_ABS)
        relativeSymOccurs = true;
}

/// assembler section
struct AsmSection
{
    cxuint kernelId;    ///< kernel id (optional)
    AsmSectionType type;        ///< type of section
    std::vector<cxbyte> content;    ///< content of section
};

/// type of clause
enum class AsmClauseType
{
    IF,     ///< if clause
    ELSEIF, ///< elseif clause
    ELSE,   ///< else clause
    REPEAT, ///< rept clause
    MACRO   ///< macro clause
};

/// assembler's clause (if,else,macro,rept)
struct AsmClause
{
    AsmClauseType type; ///< type of clause
    AsmSourcePos pos;   ///< position
    bool condSatisfied; ///< if conditional clause has already been satisfied
    AsmSourcePos prevIfPos; ///< position of previous if-clause
};

/// main class of assembler
class Assembler
{
public:
    /// defined symbol entry
    typedef std::pair<std::string, uint64_t> DefSym;
    /// macro map type
    typedef std::unordered_map<std::string, RefPtr<const AsmMacro> > MacroMap;
    /// kernel map type
    typedef std::unordered_map<std::string, cxuint> KernelMap;
private:
    friend class AsmStreamInputFilter;
    friend class AsmMacroInputFilter;
    friend class AsmExpression;
    friend class AsmFormatHandler;
    
    friend struct AsmPseudoOps; // INTERNAL LOGIC
    BinaryFormat format;
    GPUDeviceType deviceType;
    bool _64bit;    ///
    bool good;
    ISAAssembler* isaAssembler;
    std::vector<DefSym> defSyms;
    std::vector<std::string> includeDirs;
    std::vector<AsmSection> sections;
    AsmSymbolMap symbolMap;
    std::unordered_set<AsmSymbolEntry*> symbolSnapshots;
    MacroMap macroMap;
    KernelMap kernelMap;
    cxuint flags;
    uint64_t macroCount;
    
    cxuint inclusionLevel;
    cxuint macroSubstLevel;
    cxuint repetitionLevel;
    bool lineAlreadyRead; // if line already read
    
    size_t lineSize;
    const char* line;
    bool endOfAssembly;
    
    std::stack<AsmInputFilter*> asmInputFilters;
    AsmInputFilter* currentInputFilter;
    
    std::ostream& messageStream;
    std::ostream& printStream;
    
    union {
        AmdInput* amdOutput;
        GalliumInput* galliumOutput;
        std::vector<AsmSection>* rawCode;
    };
    
    std::stack<AsmClause> clauses;
    
    bool outFormatInitialized;
    
    bool inGlobal;
    bool inAmdConfig;
    cxuint currentKernel;
    cxuint& currentSection;
    uint64_t& currentOutPos;
    
    AsmSourcePos getSourcePos(LineCol lineCol) const
    {
        return { currentInputFilter->getMacroSubst(), currentInputFilter->getSource(),
            lineCol.lineNo, lineCol.colNo };
    }
    
    AsmSourcePos getSourcePos(size_t pos) const
    { return currentInputFilter->getSourcePos(pos); }
    AsmSourcePos getSourcePos(const char* string) const
    { return getSourcePos(string-line); }
    
    void printWarning(const AsmSourcePos& pos, const char* message);
    void printError(const AsmSourcePos& pos, const char* message);
    
    void printWarning(const char* linePlace, const char* message)
    { printWarning(getSourcePos(linePlace), message); }
    void printError(const char* linePlace, const char* message)
    { printError(getSourcePos(linePlace), message); }
    
    void printWarning(LineCol lineCol, const char* message)
    { printWarning(getSourcePos(lineCol), message); }
    void printError(LineCol lineCol, const char* message)
    { printError(getSourcePos(lineCol), message); }
    
    LineCol translatePos(const char* linePlace) const
    { return currentInputFilter->translatePos(linePlace-line); }
    LineCol translatePos(size_t pos) const
    { return currentInputFilter->translatePos(pos); }
    
    bool parseLiteral(uint64_t& value, const char* linePlace, const char*& outend);
    bool parseString(std::string& strarray, const char* linePlace, const char*& outend);
    
    enum class ParseState
    {
        FAILED = 0,
        PARSED,
        MISSING // missing element
    };
    
    /** parse symbol
     * \return state
     */
    ParseState parseSymbol(const char* linePlace, const char*& outend,
           AsmSymbolEntry*& entry, bool localLabel = true, bool dontCreateSymbol = false);
    bool skipSymbol(const char* linePlace, const char*& outend);
    
    bool setSymbol(AsmSymbolEntry& symEntry, uint64_t value, cxuint sectionId);
    
    bool assignSymbol(const std::string& symbolName, const char* placeAtSymbol,
                  const char* string, bool reassign = true, bool baseExpr = false);
    
    bool assignOutputCounter(const char* symbolStr, uint64_t value, cxuint sectionId,
                     cxbyte fillValue = 0);
    
    void parsePseudoOps(const std::string firstName, const char* stmtStartString,
                const char*& string);
    
    /// exitm - exit macro mode
    bool skipClauses(bool exitm = false);
    bool putMacroContent(RefPtr<AsmMacro> macro);
    bool putRepetitionContent(AsmRepeat& repeat);
    
    void initializeOutputFormat();
    
    bool pushClause(const char* string, AsmClauseType clauseType)
    {
        bool included; // to ignore
        return pushClause(string, clauseType, true, included);
    }
    bool pushClause(const char* string, AsmClauseType clauseType,
                  bool satisfied, bool& included);
     // return false when failed (for example no clauses)
    bool popClause(const char* string, AsmClauseType clauseType);
    
    /// returns false when includeLevel is too deep, throw error if failed a file opening
    bool includeFile(const char* pseudoOpStr, const std::string& filename);
    
    ParseState makeMacroSubstitution(const char* string);
    
    bool parseMacroArgValue(const char*& string, std::string& outStr);
    
    void putData(size_t size, const cxbyte* data);
    cxbyte* reserveData(size_t size, cxbyte fillValue = 0);
    
    void printWarningForRange(cxuint bits, uint64_t value, const AsmSourcePos& pos); 
    
    bool checkReservedName(const std::string& name);
protected:    
    /// helper for testing
    bool readLine();
public:
    /// constructor with filename and input stream
    /**
     * \param filename filename
     * \param input input stream
     * \param flags assembler flags
     * \param format output format type
     * \param deviceType GPU device type
     * \param msgStream stream for warnings and errors
     * \param printStream stream for printing message by .print pseudo-ops
     */
    explicit Assembler(const std::string& filename, std::istream& input, cxuint flags = 0,
              BinaryFormat format = BinaryFormat::AMD,
              GPUDeviceType deviceType = GPUDeviceType::CAPE_VERDE,
              std::ostream& msgStream = std::cerr, std::ostream& printStream = std::cout);
    /// destructor
    ~Assembler();
    
    /// get GPU device type
    GPUDeviceType getDeviceType() const
    { return deviceType; }
    /// set GPU device type
    void setDeviceType(const GPUDeviceType deviceType)
    { this->deviceType = deviceType; }
    /// get binary format
    BinaryFormat getBinaryFormat() const
    { return format; }
    /// set binary format
    void setBinaryFormat(BinaryFormat binFormat)
    { format = binFormat; }
    /// get bitness
    bool is64Bit() const
    { return _64bit; }
    /// set bitness
    void set64Bit(bool this64Bit)
    {  _64bit = this64Bit; }
    /// get flags
    cxuint getFlags() const
    { return flags; }
    /// set flags
    void setFlags(cxuint flags)
    { this->flags = flags; }
    /// get include directory list
    const std::vector<std::string>& getIncludeDirs() const
    { return includeDirs; }
    /// adds include directory
    void addIncludeDir(const std::string& includeDir);
    /// get symbols map
    const AsmSymbolMap& getSymbolMap() const
    { return symbolMap; }
    /// get sections
    const std::vector<AsmSection>& getSections() const
    { return sections; }
    /// get kernel map
    const KernelMap& getKernelMap() const
    { return kernelMap; }
    
    /// returns true if symbol contains absolute value
    bool isAbsoluteSymbol(const AsmSymbol& symbol) const;
    
    /// add initiali defsyms
    void addInitialDefSym(const std::string& symName, uint64_t name);
    /// get AMD Catalyst output
    const AmdInput* getAmdOutput() const
    { return amdOutput; }
    /// get GalliumCompute output
    const GalliumInput* getGalliumOutput() const
    { return galliumOutput; }
    /// main routine to assemble code
    bool assemble();
};

};

#endif
