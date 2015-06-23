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

#include <CLRX/Config.h>
#include <iostream>
#include <cstdio>
#include <sstream>
#include <map>
#include <memory>
#include <CLRX/utils/Containers.h>
#include <CLRX/amdasm/Assembler.h>
#include "TestUtils.h"

using namespace CLRX;

struct Section
{
    const char* kernel;
    AsmSectionType type;
    Array<cxbyte> content;
};

struct SymEntry
{
    const char* name;
    uint64_t value;
    cxuint sectionId;
    uint64_t size;
    bool isDefined;
    bool onceDefined;
    bool base;
    cxbyte info;
    cxbyte other;
};

struct AsmTestCase
{
    const char* input;
    BinaryFormat format;
    GPUDeviceType deviceType;
    bool is64Bit;
    const Array<Section> sections;
    const Array<SymEntry> symbols;
    bool good;
    const char* errorMessages;
    const char* printMessages;
};

static AsmTestCase asmTestCases1Tbl[] =
{
    /* 0 empty */
    { "", BinaryFormat::AMD, GPUDeviceType::CAPE_VERDE, false,
      {  }, { { ".", 0, 0, 0, true, false, false, 0, 0 } }, true, "", "" },
    /* 1 standard symbol assignment */
    {   R"ffDXD(sym1 = 7
        sym2 = 81
        sym3 = sym7*sym4
        sym4 = sym5*sym6+sym7 - sym1
        sym5 = 17
        sym6 = 43
        sym7 = 91)ffDXD",
        BinaryFormat::AMD, GPUDeviceType::CAPE_VERDE, false, { },
        {
            { ".", 0, 0, 0, true, false, false, 0, 0 },
            { "sym1", 7, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "sym2", 81, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "sym3", 91*(17*43+91-7), ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "sym4", 17*43+91-7, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "sym5", 17, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "sym6", 43, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "sym7", 91, ASMSECT_ABS, 0, true, false, false, 0, 0 }
        }, true, "", ""
    },
    /* 2 undefined symbols and self defined and redefinitions */
    {   R"ffDXD(sym1 = 7
        sym2 = 81
        sym3 = sym7*sym4
        sym4 = sym5*sym6+sym7 - sym1
        sym5 = 17
        sym6 = 43
        sym9 = sym9
        sym10 = sym10
        sym10 = sym2+7)ffDXD",
        BinaryFormat::AMD, GPUDeviceType::CAPE_VERDE, false, { },
        {
            { ".", 0, 0, 0, true, false, false, 0, 0 },
            { "sym1", 7, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "sym10", 88, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "sym2", 81, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "sym3", 0, ASMSECT_ABS, 0, false, false, false, 0, 0 },
            { "sym4", 0, ASMSECT_ABS, 0, false, false, false, 0, 0 },
            { "sym5", 17, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "sym6", 43, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "sym7", 0, ASMSECT_ABS, 0, false, false, false, 0, 0 },
            { "sym9", 0, ASMSECT_ABS, 0, false, false, false, 0, 0 },
        }, true, "", ""
    },
    // 3 labels and local labels
    {   R"ffDXD(.rawcode
start: .int 3,5,6
label1: vx0 = start
        vx2 = label1+6
        vx3 = label2+8
        .int 1,2,3,4
label2: .int 3,6,7
        vx4 = 2f
2:      .int 11
        vx5 = 2b
        vx6 = 2f
        vx7 = 3f
2:      .int 12
3:      vx8 = 3b
        )ffDXD",
        BinaryFormat::RAWCODE, GPUDeviceType::CAPE_VERDE, false,
        { { nullptr, AsmSectionType::RAWCODE_CODE,
            { 3, 0, 0, 0, 5, 0, 0, 0, 6, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0,
              3, 0, 0 ,0, 4, 0, 0, 0, 3, 0, 0, 0, 6, 0, 0, 0, 7, 0, 0, 0,
              11, 0, 0, 0, 12, 0, 0, 0 } } },
        {
            { ".", 48, 0, 0, true, false, false, 0, 0 },
            { "2b", 44, 0, 0, true, false, false, 0, 0 },
            { "2f", 44, 0, 0, false, false, false, 0, 0 },
            { "3b", 48, 0, 0, true, false, false, 0, 0 },
            { "3f", 48, 0, 0, false, false, false, 0, 0 },
            { "label1", 12, 0, 0, true, true, false, 0, 0 },
            { "label2", 28, 0, 0, true, true, false, 0, 0 },
            { "start", 0, 0, 0, true, true, false, 0, 0 },
            { "vx0", 0, 0, 0, true, false, false, 0, 0 },
            { "vx2", 18, 0, 0, true, false, false, 0, 0 },
            { "vx3", 36, 0, 0, true, false, false, 0, 0 },
            { "vx4", 40, 0, 0, true, false, false, 0, 0 },
            { "vx5", 40, 0, 0, true, false, false, 0, 0 },
            { "vx6", 44, 0, 0, true, false, false, 0, 0 },
            { "vx7", 48, 0, 0, true, false, false, 0, 0 },
            { "vx8", 48, 0, 0, true, false, false, 0, 0 },
        }, true, "", ""
    },
    /* 4 labels on absolute section type (likes global data) */
    {   R"ffDXD(label1:
3:      v1 = label1
        v2 = 3b)ffDXD",
        BinaryFormat::AMD, GPUDeviceType::CAPE_VERDE, false,
        { { nullptr, AsmSectionType::AMD_GLOBAL_DATA, { } } },
        {
            { ".", 0, 0, 0, true, false, false, 0, 0 },
            { "3b", 0, 0, 0, true, false, false, 0, 0 },
            { "3f", 0, 0, 0, false, false, false, 0, 0 },
            { "label1", 0, 0, 0, true, true, false, 0, 0, },
            { "v1", 0, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "v2", 0, ASMSECT_ABS, 0, true, false, false, 0, 0 }
        }, true, "", ""
    },
    /* 5 assignments, assignment of labels and symbols */
    {   R"ffDXD(.rawcode
start: .byte 0xfa, 0xfd, 0xfb, 0xda
start:  # try define again this same label
        start = 132 # try define by assignment
        .byte zx
        zx = 9
        .byte zx
        zx = 10
1:      .byte zx
        1 = 6       # illegal asssignemt of local label
        # by .set
        .byte zy
        .set zy, 10
        .byte zy
        .set zy, 11
        .byte zy
        # by .equ
        .byte zz
        .equ zz, 100
        .byte zz
        .equ zz, 120
        .byte zz
        # by equiv
        .byte testx
        .equiv testx, 130   # illegal by equiv
        .byte testx
        .equiv testx, 150
        .byte testx
        myval = 0x12
        .equiv myval,0x15   # illegal by equiv
        .equiv myval,0x15   # illegal by equiv
        myval = 6       # legal by normal assignment
        .set myval,8    # legal
        .equ myval,9    # legal
        testx = 566
        .set testx,55)ffDXD",
        BinaryFormat::RAWCODE, GPUDeviceType::CAPE_VERDE, false,
        { { nullptr, AsmSectionType::RAWCODE_CODE,
            { 0xfa, 0xfd, 0xfb, 0xda, 0x09, 0x09, 0x0a, 0x0a, 0x0a, 0x0b, 0x64, 0x64,
              0x78, 0x82, 0x82, 0x82 } } },
        {
            { ".", 16, 0, 0, true, false, false, 0, 0 },
            { "1b", 6, 0, 0, true, false, false, 0, 0 },
            { "1f", 6, 0, 0, false, false, false, 0, 0 },
            { "myval", 9, ASMSECT_ABS, 0, true, false, false, 0, 0, },
            { "start", 0, 0, 0, true, true, false, 0, 0 },
            { "testx", 130, ASMSECT_ABS, 0, true, true, false, 0, 0 },
            { "zx", 10, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "zy", 11, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "zz", 120, ASMSECT_ABS, 0, true, false, false, 0, 0 }
        }, false,
        "test.s:3:1: Error: Symbol 'start' is already defined\n"
        "test.s:4:9: Error: Symbol 'start' is already defined\n"
        "test.s:10:9: Error: Illegal number at statement begin\n"
        "test.s:10:11: Error: Garbages at end of line with pseudo-op\n"
        "test.s:27:16: Error: Symbol 'testx' is already defined\n"
        "test.s:30:16: Error: Symbol 'myval' is already defined\n"
        "test.s:31:16: Error: Symbol 'myval' is already defined\n"
        "test.s:35:9: Error: Symbol 'testx' is already defined\n"
        "test.s:36:14: Error: Symbol 'testx' is already defined\n", ""
    },
    /* 6 .eqv test 1 */
    {   R"ffDXD(        z=5
        .eqv v1,v+t
        .eqv v,z*y
        .int v1
        .int v+v
        z=8
        .int v+v
        z=9
        y=3
        t=7
        .int v1
        t=8
        y=2
        .int v1+v)ffDXD",
        BinaryFormat::AMD, GPUDeviceType::CAPE_VERDE, false,
        {  { nullptr, AsmSectionType::AMD_GLOBAL_DATA,
           { 0x16, 0, 0, 0, 0x1e, 0, 0, 0, 0x30, 0, 0, 0, 0x22, 0, 0, 0,
             0x2c, 0, 0, 0 } } },
        {
            { ".", 20, 0, 0, true, false, false, 0, 0 },
            { "t", 8, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "v", 0, ASMSECT_ABS, 0, false, true, true, 0, 0 },
            { "v1", 0, ASMSECT_ABS, 0, false, true, true, 0, 0 },
            { "y", 2, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "z", 9, ASMSECT_ABS, 0, true, false, false, 0, 0 }
        }, true, "", ""
    },
    /* 7 .eqv test 2 */
    {   R"ffDXD(.int y+7
        t=8
        tx=3
        .eqv y,t*tx+2
        
        .int y2+7
        t2=8
        .eqv y2,t2*tx2+3
        tx2=5
        
        n1=7
        n2=6
        .eqv out0,n1*n2+2
        .int out0
        n2=5
        .int out0
        
        t2=3
        t3=4
        .eqv x0,2*t2*t3
        .eqv out1,x0*2
        .int out1
        
        .eqv x1,2
        .eqv out2,x1*2
        .int out2)ffDXD",
        BinaryFormat::AMD, GPUDeviceType::CAPE_VERDE, false,
        {  { nullptr, AsmSectionType::AMD_GLOBAL_DATA,
           { 0x21, 0, 0, 0, 0x32, 0, 0, 0, 0x2c, 0, 0, 0, 0x25, 0, 0, 0,
             0x30, 0, 0, 0, 0x04, 0, 0, 0 } } },
        {
            { ".", 24, 0, 0, true, false, false, 0, 0 },
            { "n1", 7, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "n2", 5, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "out0", 0, ASMSECT_ABS, 0, false, true, true, 0, 0 },
            { "out1", 0, ASMSECT_ABS, 0, false, true, true, 0, 0 },
            { "out2", 0, ASMSECT_ABS, 0, false, true, true, 0, 0 },
            { "t", 8, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "t2", 3, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "t3", 4, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "tx", 3, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "tx2", 5, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "x0", 0, ASMSECT_ABS, 0, false, true, true, 0, 0 },
            { "x1", 2, ASMSECT_ABS, 0, true, true, false, 0, 0 },
            { "y", 0, ASMSECT_ABS, 0, false, true, true, 0, 0 },
            { "y2", 0, ASMSECT_ABS, 0, false, true, true, 0, 0 }
        }, true, "", ""
    },
    /* 8 .eqv test3 - various order of .eqv */
    {   R"ffDXD(x00t = 6
            x00u = x00t+9
            .eqv x03,6
            .eqv x02,x03+2*x03+x00u
            .eqv x01,x02*x02+x00t
            x00 = x01+x02*x03
            .int x00
            z00 = x00
            
            x10u = x10t+11
            x10t = 8
            .eqv x12,x13+2*x13+x10u
            .eqv x13,14
            .eqv x11,x12*x12+x10t
            x10 = x11+x12*x13
            .int x10
            z10 = x10
            
            x20u = x20t+3
            x20t = 11
            .eqv x21,x22*x22+x20t
            .eqv x22,x23+2*x23+x20u
            .eqv x23,78
            x20 = x21+x22*x23
            .int x20
            z20 = x20
            
            x30u = x30t+21
            x30t = 31
            x30 = x31+x32*x33
            .eqv x31,x32*x32+x30t
            .eqv x32,x33+2*x33+x30u
            .eqv x33,5
            .int x30
            z30 = x30
            
            z40 = x40
            .int x40
            x40u = x40t+71
            x40t = 22
            x40 = x41+x42*x43
            .eqv x41,x42*x42+x40t
            .eqv x42,x43+2*x43+x40u
            .eqv x43,12
            
            z50 = x50
            .int x50
            x50t = 15
            x50 = x51+x52*x53
            .eqv x51,x52*x52+x50t
            .eqv x52,x53+2*x53+x50u
            .eqv x53,23
            x50u = x50t+19
            )ffDXD", /* TODO: GNU as incorrectly calculates x40 and x50 symbols */
        BinaryFormat::AMD, GPUDeviceType::CAPE_VERDE, false,
        {  { nullptr, AsmSectionType::AMD_GLOBAL_DATA,
           { 0x0d, 0x5, 0, 0, 0xe7, 0x11, 0, 0, 0xdb, 0x3b, 1, 0, 0xf7, 0x12, 0, 0,
             0x23, 0x47, 0, 0, 0xc1, 0x32, 0, 0 } } },
        {
            { ".", 24, 0, 0, true, false, false, 0, 0 },
            { "x00", 1293, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "x00t", 6, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "x00u", 15, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "x01", 0, ASMSECT_ABS, 0, false, true, true, 0, 0 },
            { "x02", 0, ASMSECT_ABS, 0, false, true, true, 0, 0 },
            { "x03", 6, ASMSECT_ABS, 0, true, true, false, 0, 0 },
            { "x10", 4583, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "x10t", 8, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "x10u", 19, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "x11", 0, ASMSECT_ABS, 0, false, true, true, 0, 0 },
            { "x12", 0, ASMSECT_ABS, 0, false, true, true, 0, 0 },
            { "x13", 14, ASMSECT_ABS, 0, true, true, false, 0, 0 },
            { "x20", 80859, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "x20t", 11, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "x20u", 14, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "x21", 0, ASMSECT_ABS, 0, false, true, true, 0, 0 },
            { "x22", 0, ASMSECT_ABS, 0, false, true, true, 0, 0 },
            { "x23", 78, ASMSECT_ABS, 0, true, true, false, 0, 0 },
            { "x30", 4855, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "x30t", 31, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "x30u", 52, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "x31", 0, ASMSECT_ABS, 0, false, true, true, 0, 0 },
            { "x32", 0, ASMSECT_ABS, 0, false, true, true, 0, 0 },
            { "x33", 5, ASMSECT_ABS, 0, true, true, false, 0, 0 },
            { "x40", 18211, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "x40t", 22, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "x40u", 93, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "x41", 0, ASMSECT_ABS, 0, false, true, true, 0, 0 },
            { "x42", 0, ASMSECT_ABS, 0, false, true, true, 0, 0 },
            { "x43", 12, ASMSECT_ABS, 0, true, true, false, 0, 0 },
            { "x50", 12993, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "x50t", 15, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "x50u", 34, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "x51", 0, ASMSECT_ABS, 0, false, true, true, 0, 0 },
            { "x52", 0, ASMSECT_ABS, 0, false, true, true, 0, 0 },
            { "x53", 23, ASMSECT_ABS, 0, true, true, false, 0, 0 },
            { "z00", 1293, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "z10", 4583, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "z20", 80859, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "z30", 4855, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "z40", 18211, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "z50", 12993, ASMSECT_ABS, 0, true, false, false, 0, 0 }
        }, true, "", ""
    },
    /* 9 .eqv test3 - various order of .eqv */
    {   R"ffDXD(x00t = 6
            x00u = x00t+9
            .eqv x03,x00u*x00t+6
            .eqv x02,x03+2*x03+x00u
            .eqv x01,x02*x02+x00t
            x00 = x01+x02*x03
            .int x00
            z00 = x00
            
            x10u = x10t+11
            x10t = 8
            .eqv x12,x13+2*x13+x10u
            .eqv x13,x10u*x10t+14
            .eqv x11,x12*x12+x10t
            x10 = x11+x12*x13
            .int x10
            z10 = x10
            
            x20u = x20t+3
            x20t = 11
            .eqv x21,x22*x22+x20t
            .eqv x22,x23+2*x23+x20u
            .eqv x23,x20u*x20t+78
            x20 = x21+x22*x23
            .int x20
            z20 = x20
            
            x30u = x30t+21
            x30t = 31
            x30 = x31+x32*x33
            .eqv x31,x32*x32+x30t
            .eqv x32,x33+2*x33+x30u
            .eqv x33,x30u*x30t+5
            .int x30
            z30 = x30
            
            z40 = x40
            .int x40
            x40u = x40t+71
            x40t = 22
            x40 = x41+x42*x43
            .eqv x41,x42*x42+x40t
            .eqv x42,x43+2*x43+x40u
            .eqv x43,x40u*x40t+12
            
            z50 = x50
            .int x50
            x50t = 15
            x50 = x51+x52*x53
            .eqv x51,x52*x52+x50t
            .eqv x52,x53+2*x53+x50u
            .eqv x53,x50u*x50t+23
            x50u = x50t+19
            )ffDXD", /* TODO: GNU as incorrectly calculates x40 and x50 symbols */
        BinaryFormat::AMD, GPUDeviceType::CAPE_VERDE, false,
        {  { nullptr, AsmSectionType::AMD_GLOBAL_DATA,
           { 0x47, 0xd8, 0x01, 0x00, 0x5f, 0x63, 0x05, 0x00, 0x9f, 0x34, 0x0a, 0x00,
             0x67, 0xc9, 0xe7, 0x01, 0xfd, 0x17, 0x1c, 0x03, 0xc5, 0xf8, 0x35, 0x00 } } },
        {
            { ".", 24, 0, 0, true, false, false, 0, 0 },
            { "x00", 120903U, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "x00t", 6, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "x00u", 15, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "x01", 0, ASMSECT_ABS, 0, false, true, true, 0, 0 },
            { "x02", 0, ASMSECT_ABS, 0, false, true, true, 0, 0 },
            { "x03", 0, ASMSECT_ABS, 0, false, true, true, 0, 0 },
            { "x10", 353119U, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "x10t", 8, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "x10u", 19, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "x11", 0, ASMSECT_ABS, 0, false, true, true, 0, 0 },
            { "x12", 0, ASMSECT_ABS, 0, false, true, true, 0, 0 },
            { "x13", 0, ASMSECT_ABS, 0, false, true, true, 0, 0 },
            { "x20", 668831U, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "x20t", 11, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "x20u", 14, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "x21", 0, ASMSECT_ABS, 0, false, true, true, 0, 0 },
            { "x22", 0, ASMSECT_ABS, 0, false, true, true, 0, 0 },
            { "x23", 0, ASMSECT_ABS, 0, false, true, true, 0, 0 },
            { "x30", 31967591U, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "x30t", 31, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "x30u", 52, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "x31", 0, ASMSECT_ABS, 0, false, true, true, 0, 0 },
            { "x32", 0, ASMSECT_ABS, 0, false, true, true, 0, 0 },
            { "x33", 0, ASMSECT_ABS, 0, false, true, true, 0, 0 },
            { "x40", 52172797U, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "x40t", 22, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "x40u", 93, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "x41", 0, ASMSECT_ABS, 0, false, true, true, 0, 0 },
            { "x42", 0, ASMSECT_ABS, 0, false, true, true, 0, 0 },
            { "x43", 0, ASMSECT_ABS, 0, false, true, true, 0, 0 },
            { "x50", 3537093U, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "x50t", 15, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "x50u", 34, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "x51", 0, ASMSECT_ABS, 0, false, true, true, 0, 0 },
            { "x52", 0, ASMSECT_ABS, 0, false, true, true, 0, 0 },
            { "x53", 0, ASMSECT_ABS, 0, false, true, true, 0, 0 },
            { "z00", 120903U, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "z10", 353119U, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "z20", 668831U, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "z30", 31967591U, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "z40", 52172797U, ASMSECT_ABS, 0, true, false, false, 0, 0 },
            { "z50", 3537093U, ASMSECT_ABS, 0, true, false, false, 0, 0 }
        }, true, "", ""
    }
};

static void testAssembler(cxuint testId, const AsmTestCase& testCase)
{
    std::istringstream input(testCase.input);
    std::ostringstream errorStream;
    std::ostringstream printStream;
    
    Assembler assembler("test.s", input, ASM_ALL, BinaryFormat::AMD,
            GPUDeviceType::CAPE_VERDE, errorStream, printStream);
    bool good = assembler.assemble();
    /* compare results */
    char testName[30];
    snprintf(testName, 30, "Test #%u", testId);
    
    assertValue(testName, "good", int(testCase.good), int(good));
    assertValue(testName, "format", int(testCase.format),
                int(assembler.getBinaryFormat()));
    assertValue(testName, "deviceType", int(testCase.deviceType),
                int(assembler.getDeviceType()));
    assertValue(testName, "64bit", int(testCase.is64Bit), int(assembler.is64Bit()));
    
    // check sections
    const std::vector<AsmSection*>& resSections = assembler.getSections();
    assertValue(testName, "sections.length", testCase.sections.size(), resSections.size());
    for (size_t i = 0; i < testCase.sections.size(); i++)
    {
        std::ostringstream caseOss;
        caseOss << "Section#" << i << ".";
        caseOss.flush();
        std::string caseName(caseOss.str());
        
        const AsmSection& resSection = *(resSections[i]);
        const Section& expSection = testCase.sections[i];
        assertValue(testName, caseName+"type", int(expSection.type), int(resSection.type));
        assertArray<cxbyte>(testName, caseName+".content", expSection.content,
                    resSection.content);
    }
    // check symbols
    const AsmSymbolMap& resSymbolMap = assembler.getSymbolMap();
    assertValue(testName, "symbols.length", testCase.symbols.size(), resSymbolMap.size());
    
    std::vector<const AsmSymbolEntry*> symEntries;
    for (const AsmSymbolEntry& symEntry: assembler.getSymbolMap())
        symEntries.push_back(&symEntry);
    std::sort(symEntries.begin(), symEntries.end(),
                [](const AsmSymbolEntry* s1, const AsmSymbolEntry* s2)
                { return s1->first < s2->first; });
    
    for (size_t i = 0; i < testCase.symbols.size(); i++)
    {
        std::ostringstream caseOss;
        caseOss << "Symbol#" << i << ".";
        caseOss.flush();
        std::string caseName(caseOss.str());
        
        const AsmSymbolEntry& resSymbol = *(symEntries[i]);
        const SymEntry& expSymbol = testCase.symbols[i];
        assertString(testName,caseName+"name", expSymbol.name, resSymbol.first);
        assertValue(testName,caseName+"value", expSymbol.value, resSymbol.second.value);
        assertValue(testName,caseName+"sectId", expSymbol.sectionId,
                     resSymbol.second.sectionId);
        assertValue(testName,caseName+"size", expSymbol.size, resSymbol.second.size);
        assertValue(testName,caseName+"isDefined", int(expSymbol.isDefined),
                    int(resSymbol.second.isDefined));
        assertValue(testName,caseName+"onceDefined", int(expSymbol.onceDefined),
                    int(resSymbol.second.onceDefined));
        assertValue(testName,caseName+"base", int(expSymbol.base),
                    int(resSymbol.second.base));
        assertValue(testName,caseName+"info", int(expSymbol.info),
                    int(resSymbol.second.info));
        assertValue(testName,caseName+"other", int(expSymbol.other),
                    int(resSymbol.second.other));
    }
    errorStream.flush();
    printStream.flush();
    const std::string errorMsgs = errorStream.str();
    const std::string printMsgs = printStream.str();
    assertString(testName, "errorMessages", testCase.errorMessages, errorMsgs);
    assertString(testName, "printMessages", testCase.printMessages, printMsgs);
}

int main(int argc, const char** argv)
{
    int retVal = 0;
    for (size_t i = 0; i < sizeof(asmTestCases1Tbl)/sizeof(AsmTestCase); i++)
        try
        { testAssembler(i, asmTestCases1Tbl[i]); }
        catch(const std::exception& ex)
        {
            std::cerr << ex.what() << std::endl;
            retVal = 1;
        }
    return retVal;
}
