#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <cmath>
#include <algorithm>
#include <stdlib.h>
#include <time.h>

#include "memory.h"
#include "cpu.h"
#include "assembler.h"
#include "compiler.h"
#include "operators.h"
#include "linker.h"


namespace Operators
{
    bool _nextTempVar = true;

    std::vector<std::string> _operators;

    std::vector<std::string>& getOperators(void) {return _operators;}


    bool initialise(void)
    {
        _nextTempVar = true;

        // Operators
        _operators.push_back(" AND ");
        _operators.push_back(" XOR ");
        _operators.push_back(" OR " );
        _operators.push_back(" NOT ");
        _operators.push_back(" MOD ");
        _operators.push_back(" LSL ");
        _operators.push_back(" LSR ");
        _operators.push_back(" ASR ");
        _operators.push_back("<<"   );
        _operators.push_back(">>"   );

        return true;
    }


    void createTmpVar(Expression::Numeric& numeric)
    {
        numeric._value = uint8_t(Compiler::getTempVarStart());
        numeric._varType = Expression::TmpVar;
        numeric._name = Compiler::getTempVarStartStr();
    }

    void createSingleOp(const std::string& opcodeStr, Expression::Numeric& numeric)
    {
        switch(numeric._varType)
        {
            // Temporary variable address
            case Expression::TmpVar:
            {
                Compiler::emitVcpuAsm(opcodeStr, Expression::byteToHexString(uint8_t(std::lround(numeric._value))), false);
            }
            break;

            // User variable name
            case Expression::IntVar:
            {
                Compiler::emitVcpuAsmUserVar(opcodeStr, numeric, false);
            }
            break;

            default: break;
        }
    }

    void handleSingleOp(const std::string& opcodeStr, Expression::Numeric& numeric)
    {
        switch(numeric._varType)
        {
            // Temporary variable address
            case Expression::TmpVar:
            {
                Compiler::emitVcpuAsm(opcodeStr, Expression::byteToHexString(uint8_t(std::lround(numeric._value))), false);
            }
            break;

            // User variable name
            case Expression::IntVar:
            {
                Compiler::emitVcpuAsmUserVar(opcodeStr, numeric, false);
            }
            break;

            default: break;
        }

        createTmpVar(numeric);
    }

    bool handleDualOp(const std::string& opcodeStr, Expression::Numeric& lhs, Expression::Numeric& rhs, bool outputHex)
    {
        std::string opcode = std::string(opcodeStr);

        // Swap left and right to take advantage of LDWI for 16bit numbers
        if(rhs._varType == Expression::Number  &&  uint16_t(rhs._value) > 255)
        {
            std::swap(lhs, rhs);
            if(opcode == "SUB")
            {
                opcode = "ADD";
                if(lhs._value > 0) lhs._value = -lhs._value;
            }
        }

        // LHS
        if(lhs._varType == Expression::Number)
        {
            // 8bit positive constants
            if(lhs._value >=0  &&  lhs._value <= 255)
            {
                (outputHex) ? Compiler::emitVcpuAsm("LDI", Expression::byteToHexString(uint8_t(std::lround(lhs._value))), false) : Compiler::emitVcpuAsm("LDI", std::to_string(uint8_t(std::lround(lhs._value))), false);
            }
            // 16bit constants
            else
            {
                (outputHex) ? Compiler::emitVcpuAsm("LDWI", Expression::wordToHexString(int16_t(std::lround(lhs._value))), false) : Compiler::emitVcpuAsm("LDWI", std::to_string(int16_t(std::lround(lhs._value))), false);
            }

            _nextTempVar = true;
        }
        else
        {
            switch(lhs._varType)
            {
                // Temporary variable address
                case Expression::TmpVar:
                {
                    Compiler::emitVcpuAsm("LDW", Expression::byteToHexString(uint8_t(std::lround(lhs._value))), false);
                }
                break;

                // User variable name
                case Expression::IntVar:
                {
                    if(!Compiler::emitVcpuAsmUserVar("LDW", lhs, true)) return false;
                    _nextTempVar = false;
                }
                break;

                default: break;
            }
        }

        // RHS
        if(rhs._varType == Expression::Number)
        {
            Compiler::emitVcpuAsm(opcode + "I", std::to_string(int16_t(std::lround(rhs._value))), false);
        }
        else
        {
            switch(rhs._varType)
            {
                // Temporary variable address
                case Expression::TmpVar:
                {
                    Compiler::emitVcpuAsm(opcode + "W", Expression::byteToHexString(uint8_t(std::lround(rhs._value))), false);
                }
                break;

                // User variable name
                case Expression::IntVar:
                {
                    if(!Compiler::emitVcpuAsmUserVar(opcode + "W", rhs, _nextTempVar)) return false;
                    _nextTempVar = false;
                }
                break;

                default: break;
            }
        }

        createTmpVar(lhs);
        Compiler::emitVcpuAsm("STW", Expression::byteToHexString(uint8_t(Compiler::getTempVarStart())), false);

        return true;
    }

    bool handleLogicalOp(const std::string& opcode, Expression::Numeric& lhs)
    {
        // SYS shift function needs this preamble, LSLW doesn't
        switch(lhs._varType)
        {
            // Temporary variable address
            case Expression::TmpVar:
            {
                Compiler::emitVcpuAsm("LDW", Expression::byteToHexString(uint8_t(std::lround(lhs._value))), false);
            }
            break;

            // User variable name
            case Expression::IntVar:
            {
                if(!Compiler::emitVcpuAsmUserVar("LDW", lhs, true)) return false;
            }
            break;

            default: break;
        }

        if(opcode != "LSLW"  &&  opcode != "<<") Compiler::emitVcpuAsm("STW", "mathShift", false);

        createTmpVar(lhs);

        return true;
    }

    void emitCcType(Expression::CCType ccType, std::string& cc)
    {
        switch(ccType)
        {
            case Expression::BooleanCC:
            {
                if(Compiler::getCodeRomType() >= Cpu::ROMv5a)
                {
                    Compiler::emitVcpuAsm("CALLI", "convert" + cc + "Op", false);
                }
                else
                {
                    Compiler::emitVcpuAsm("CALL", "convert" + cc + "OpAddr", false);

                    // Enable system internal sub intialiser and mark system internal sub to be loaded, (init functions are not needed for ROMv5a and higher as CALLI is able to directly CALL them)
                    if(Compiler::getCodeRomType() < Cpu::ROMv5a) Compiler::enableSysInitFunc("Init" + cc + "Op");
                    Linker::setInternalSubToLoad("convert" + cc + "Op");
                }
            }
            break;

            case Expression::NormalCC: Compiler::emitVcpuAsm("%Jump" + Expression::strToUpper(cc), "", false); break;
            case Expression::FastCC: Compiler::emitVcpuAsm("B" + Expression::strToUpper(cc), "", false); break;

            default: break;
        }
    }
    bool handleConditionOp(Expression::Numeric& lhs, Expression::Numeric& rhs, Expression::CCType ccType, bool& invertedLogic, const std::string& opcode="SUB")
    {
        // Swap left and right to take advantage of LDWI for 16bit numbers
        invertedLogic = false;
        if(rhs._varType == Expression::Number  &&  uint16_t(rhs._value) > 255)
        {
            std::swap(lhs, rhs);
            invertedLogic = true;
        }

        // JumpCC and BCC are inverses of each other
        lhs._ccType = ccType;
        if(ccType == Expression::FastCC) invertedLogic = !invertedLogic;

        // LHS
        if(lhs._varType == Expression::Number)
        {
            // 8bit positive constants
            if(lhs._value >=0  &&  lhs._value <= 255)
            {
                Compiler::emitVcpuAsm("LDI", std::to_string(uint8_t(std::lround(lhs._value))), false);
            }
            // 16bit constants
            else
            {
                Compiler::emitVcpuAsm("LDWI", std::to_string(int16_t(std::lround(lhs._value))), false);
            }

            _nextTempVar = true;
        }
        else
        {
            switch(lhs._varType)
            {
                // Temporary variable address
                case Expression::TmpVar:
                {
                    Compiler::emitVcpuAsm("LDW", Expression::byteToHexString(uint8_t(std::lround(lhs._value))), false);
                }
                break;

                // User variable name
                case Expression::IntVar:
                {
                    if(!Compiler::emitVcpuAsmUserVar("LDW", lhs, true)) return false;
                    _nextTempVar = false;
                }
                break;

                default: break;
            }
        }

        // RHS
        if(rhs._varType == Expression::Number)
        {
            Compiler::emitVcpuAsm(opcode + "I", std::to_string(int16_t(std::lround(rhs._value))), false);
        }
        else        
        {
            switch(rhs._varType)
            {
                // Temporary variable address
                case Expression::TmpVar:
                {
                    Compiler::emitVcpuAsm(opcode + "W", Expression::byteToHexString(uint8_t(std::lround(rhs._value))), false);
                }
                break;

                // User variable name
                case Expression::IntVar:
                {
                    if(!Compiler::emitVcpuAsmUserVar(opcode + "W", rhs, _nextTempVar)) return false;
                    _nextTempVar = false;
                }
                break;

                default: break;
            }
        }

        createTmpVar(lhs);

        return true;
    }

    bool handleMathOp(const std::string& opcode, const std::string& operand, Expression::Numeric& lhs, Expression::Numeric& rhs, bool isMod=false)
    {
        // LHS
        if(lhs._varType == Expression::Number)
        {
            // 8bit positive constants
            if(lhs._value >=0  &&  lhs._value <= 255)
            {
                Compiler::emitVcpuAsm("LDI", std::to_string(uint8_t(std::lround(lhs._value))), false);
            }
            // 16bit constants
            else
            {
                Compiler::emitVcpuAsm("LDWI", std::to_string(int16_t(std::lround(lhs._value))), false);
            }

            _nextTempVar = true;
        }
        else
        {
            switch(lhs._varType)
            {
                // Temporary variable address
                case Expression::TmpVar:
                {
                    Compiler::emitVcpuAsm("LDW", Expression::byteToHexString(uint8_t(std::lround(lhs._value))), false);
                }
                break;

                // User variable name
                case Expression::IntVar:
                {
                    if(!Compiler::emitVcpuAsmUserVar("LDW", lhs, true)) return false;
                    _nextTempVar = false;
                }
                break;

                default: break;
            }
        }

        Compiler::emitVcpuAsm("STW", "mathX", false);

        // RHS
        if(rhs._varType == Expression::Number)
        {
            if(rhs._value >=0  &&  rhs._value <= 255)
            {
                Compiler::emitVcpuAsm("LDI", std::to_string(uint8_t(std::lround(rhs._value))), false);
            }
            else
            {
                Compiler::emitVcpuAsm("LDWI", std::to_string(int16_t(std::lround(rhs._value))), false);
            }
        }
        else
        {
            switch(rhs._varType)
            {
                // Temporary variable address
                case Expression::TmpVar:
                {
                    Compiler::emitVcpuAsm("LDW", Expression::byteToHexString(uint8_t(std::lround(rhs._value))), false);
                }
                break;

                // User variable name
                case Expression::IntVar:
                {
                    if(!Compiler::emitVcpuAsmUserVar("LDW", rhs, _nextTempVar)) return false;
                    _nextTempVar = false;
                }
                break;

                default: break;
            }
        }

        Compiler::emitVcpuAsm("STW", "mathY", false);

        if(Compiler::getCodeRomType() >= Cpu::ROMv5a)
        {
            Compiler::emitVcpuAsm(opcode, operand, false);
        }
        else
        {
            Compiler::emitVcpuAsm("LDWI", operand, false);
            Compiler::emitVcpuAsm(opcode, "giga_vAC", false);
        }

        createTmpVar(lhs);
        
        if(isMod) Compiler::emitVcpuAsm("LDW", "mathRem", false);
        Compiler::emitVcpuAsm("STW", Expression::byteToHexString(uint8_t(Compiler::getTempVarStart())), false);

        return true;
    }

    uint32_t handleRevOp(uint32_t input, uint32_t n)
    {
        uint32_t output = 0;
        uint32_t bits = input & uint32_t(pow(2, n) - 1);
        for(uint32_t i=0; i<=n-1; i++)
        {
            output = (output << 1) | (bits & 1);
            bits = bits >> 1;
        }

        return output;
    }


    // ********************************************************************************************
    // Unary Operators
    // ********************************************************************************************
    Expression::Numeric POS(Expression::Numeric& numeric)
    {
        if(numeric._varType == Expression::Number)
        {
            numeric._value = +numeric._value;
            return numeric;
        }

        Compiler::getNextTempVar();
        handleSingleOp("LDW", numeric);
        Compiler::emitVcpuAsm("STW", Expression::byteToHexString(uint8_t(Compiler::getTempVarStart())), false);
        
        return numeric;
    }

    Expression::Numeric NEG(Expression::Numeric& numeric)
    {
        if(numeric._varType == Expression::Number)
        {
            numeric._value = -numeric._value;
            return numeric;
        }

        Compiler::getNextTempVar();
        Compiler::emitVcpuAsm("LDI", std::to_string(0), false);
        handleSingleOp("SUBW", numeric);
        Compiler::emitVcpuAsm("STW", Expression::byteToHexString(uint8_t(Compiler::getTempVarStart())), false);
        
        return numeric;
    }

    Expression::Numeric NOT(Expression::Numeric& numeric)
    {
        if(numeric._varType == Expression::Number)
        {
            numeric._value = ~int16_t(std::lround(numeric._value));
            return numeric;
        }

        Compiler::getNextTempVar();
        Compiler::emitVcpuAsm("LDWI", std::to_string(-1), false);
        handleSingleOp("SUBW", numeric);
        Compiler::emitVcpuAsm("STW", Expression::byteToHexString(uint8_t(Compiler::getTempVarStart())), false);

        return numeric;
    }


    // ********************************************************************************************
    // Unary Math Operators
    // ********************************************************************************************
    Expression::Numeric CEIL(Expression::Numeric& numeric)
    {
        if(numeric._varType == Expression::Number)
        {
            numeric._value = ceil(numeric._value);
        }

        return numeric;
    }

    Expression::Numeric FLOOR(Expression::Numeric& numeric)
    {
        if(numeric._varType == Expression::Number)
        {
            numeric._value = floor(numeric._value);
        }

        return numeric;
    }

    Expression::Numeric POWF(Expression::Numeric& numeric)
    {
        if(numeric._varType == Expression::Number  &&  numeric._parameters.size() > 0  &&  numeric._parameters[0]._varType == Expression::Number)
        {
            numeric._value = pow(numeric._value, numeric._parameters[0]._value);
        }

        return numeric;
    }

    Expression::Numeric SQRT(Expression::Numeric& numeric)
    {
        if(numeric._varType == Expression::Number  &&  numeric._value > 0.0)
        {
            numeric._value = sqrt(numeric._value);
        }

        return numeric;
    }

    Expression::Numeric EXP(Expression::Numeric& numeric)
    {
        if(numeric._varType == Expression::Number)
        {
            numeric._value = exp(numeric._value);
        }

        return numeric;
    }

    Expression::Numeric EXP2(Expression::Numeric& numeric)
    {
        if(numeric._varType == Expression::Number)
        {
            numeric._value = exp2(numeric._value);
        }

        return numeric;
    }

    Expression::Numeric LOG(Expression::Numeric& numeric)
    {
        if(numeric._varType == Expression::Number  &&  numeric._value > 0.0)
        {
            numeric._value = log(numeric._value);
        }

        return numeric;
    }

    Expression::Numeric LOG2(Expression::Numeric& numeric)
    {
        if(numeric._varType == Expression::Number  &&  numeric._value > 0.0)
        {
            numeric._value = log2(numeric._value);
        }

        return numeric;
    }

    Expression::Numeric LOG10(Expression::Numeric& numeric)
    {
        if(numeric._varType == Expression::Number  &&  numeric._value > 0.0)
        {
            numeric._value = log10(numeric._value);
        }

        return numeric;
    }

    Expression::Numeric SIN(Expression::Numeric& numeric)
    {
        if(numeric._varType == Expression::Number)
        {
            numeric._value = sin(numeric._value*MATH_PI/180.0);
        }

        return numeric;
    }

    Expression::Numeric COS(Expression::Numeric& numeric)
    {
        if(numeric._varType == Expression::Number)
        {
            numeric._value = cos(numeric._value*MATH_PI/180.0);
        }

        return numeric;
    }

    Expression::Numeric TAN(Expression::Numeric& numeric)
    {
        if(numeric._varType == Expression::Number)
        {
            numeric._value = tan(numeric._value*MATH_PI/180.0);
        }

        return numeric;
    }

    Expression::Numeric ASIN(Expression::Numeric& numeric)
    {
        if(numeric._varType == Expression::Number)
        {
            numeric._value = asin(numeric._value)/MATH_PI*180.0;
        }

        return numeric;
    }

    Expression::Numeric ACOS(Expression::Numeric& numeric)
    {
        if(numeric._varType == Expression::Number)
        {
            numeric._value = acos(numeric._value)/MATH_PI*180.0;;
        }

        return numeric;
    }

    Expression::Numeric ATAN(Expression::Numeric& numeric)
    {
        if(numeric._varType == Expression::Number)
        {
            numeric._value = atan(numeric._value)/MATH_PI*180.0;
        }

        return numeric;
    }

    Expression::Numeric ATAN2(Expression::Numeric& numeric)
    {
        if(numeric._varType == Expression::Number  &&  numeric._parameters.size() > 0  &&  numeric._parameters[0]._varType == Expression::Number)
        {
            if(numeric._value != 0.0  ||  numeric._parameters[0]._value != 0.0)
            {
                numeric._value = atan2(numeric._value, numeric._parameters[0]._value)/MATH_PI*180.0;
            }
        }

        return numeric;
    }

    Expression::Numeric RAND(Expression::Numeric& numeric)
    {
        if(numeric._varType == Expression::Number)
        {
            numeric._value = double(rand() % std::lround(numeric._value));
        }

        return numeric;
    }

    Expression::Numeric REV16(Expression::Numeric& numeric)
    {
        if(numeric._varType == Expression::Number)
        {
            numeric._value = double(handleRevOp(uint32_t(std::lround(numeric._value)), 16));
        }

        return numeric;
    }

    Expression::Numeric REV8(Expression::Numeric& numeric)
    {
        if(numeric._varType == Expression::Number)
        {
            numeric._value = double(handleRevOp(uint32_t(std::lround(numeric._value)), 8));
        }

        return numeric;
    }

    Expression::Numeric REV4(Expression::Numeric& numeric)
    {
        if(numeric._varType == Expression::Number)
        {
            numeric._value = double(handleRevOp(uint32_t(std::lround(numeric._value)), 4));
        }

        return numeric;
    }


    // ********************************************************************************************
    // Binary Operators
    // ********************************************************************************************
    Expression::Numeric ADD(Expression::Numeric& left, Expression::Numeric& right)
    {
        if(left._varType == Expression::Number  &&  right._varType == Expression::Number)
        {
            left._value += right._value;
            return left;
        }

        left._isValid = handleDualOp("ADD", left, right, false);
        return left;
    }

    Expression::Numeric SUB(Expression::Numeric& left, Expression::Numeric& right)
    {
        if(left._varType == Expression::Number  &&  right._varType == Expression::Number)
        {
            left._value -= right._value;
            return left;
        }

        left._isValid = handleDualOp("SUB", left, right, false);
        return left;
    }

    Expression::Numeric AND(Expression::Numeric& left, Expression::Numeric& right)
    {
        if(left._varType == Expression::Number  &&  right._varType == Expression::Number)
        {
            left._value = int16_t(std::lround(left._value)) & int16_t(std::lround(right._value));
            return left;
        }

        left._isValid = handleDualOp("AND", left, right, true);
        return left;
    }

    Expression::Numeric XOR(Expression::Numeric& left, Expression::Numeric& right)
    {
        if(left._varType == Expression::Number  &&  right._varType == Expression::Number)
        {
            left._value = int16_t(std::lround(left._value)) ^ int16_t(std::lround(right._value));
            return left;
        }

        left._isValid = handleDualOp("XOR", left, right, true);
        return left;
    }

    Expression::Numeric OR(Expression::Numeric& left, Expression::Numeric& right)
    {
        if(left._varType == Expression::Number  &&  right._varType == Expression::Number)
        {
            left._value = int16_t(std::lround(left._value)) | int16_t(std::lround(right._value));
            return left;
        }

        left._isValid = handleDualOp("OR", left, right, true);
        return left;
    }


    // ********************************************************************************************
    // Logical Operators
    // ********************************************************************************************
    Expression::Numeric LSL(Expression::Numeric& left, Expression::Numeric& right)
    {
        if(left._varType == Expression::Number  &&  right._varType == Expression::Number)
        {
            left._value = (int16_t(std::lround(left._value)) << int16_t(std::lround(right._value))) & 0x0000FFFF;
            return left;
        }

        Compiler::getNextTempVar();

        if((left._varType == Expression::TmpVar  ||  left._varType == Expression::IntVar)  &&  right._varType == Expression::Number)
        {
            if(right._value == 8)
            {
                switch(left._varType)
                {
                    // Temporary variable address
                    case Expression::TmpVar:
                    {
                        Compiler::emitVcpuAsm("LD", Expression::byteToHexString(uint8_t(std::lround(left._value))), false);
                    }
                    break;

                    // User variable name
                    case Expression::IntVar:
                    {
                        int varIndex = Compiler::findVar(left._name);
                        if(varIndex == -1) fprintf(stderr, "Operator::LSL() : couldn't find variable name '%s'\n", left._name.c_str());
                        Compiler::emitVcpuAsm("LD", "_" + left._name, false);
                    }

                    default: break;
                }

                createTmpVar(left);

                Compiler::emitVcpuAsm("ST", "giga_vAC + 1", false);
                Compiler::emitVcpuAsm("ORI", "0xFF", false);
                Compiler::emitVcpuAsm("XORI", "0xFF", false);
            }
            else
            {
                std::string opcode;
                switch(int16_t(std::lround(right._value)))
                {
                    case 1:
                    case 2:
                    case 3:
                    case 4: opcode = "LSLW"; break;

                    case 5:
                    case 6:
                    case 7: Compiler::emitVcpuAsm("LSLW", "", false); Compiler::emitVcpuAsm("LSLW", "", false); Compiler::emitVcpuAsm("LSLW", "", false); Compiler::emitVcpuAsm("LSLW", "", false); break;

                    default: break;
                }

                handleLogicalOp(opcode, left);

                Compiler::emitVcpuAsm(opcode, "", false);

                switch(int16_t(std::lround(right._value)))
                {
                    case 2: Compiler::emitVcpuAsm("LSLW", "", false);                                                                                     break;
                    case 3: Compiler::emitVcpuAsm("LSLW", "", false); Compiler::emitVcpuAsm("LSLW", "", false);                                           break;
                    case 4: Compiler::emitVcpuAsm("LSLW", "", false); Compiler::emitVcpuAsm("LSLW", "", false); Compiler::emitVcpuAsm("LSLW", "", false); break;
                    case 5: Compiler::emitVcpuAsm("LSLW", "", false);                                                                                     break;
                    case 6: Compiler::emitVcpuAsm("LSLW", "", false); Compiler::emitVcpuAsm("LSLW", "", false);                                           break;
                    case 7: Compiler::emitVcpuAsm("LSLW", "", false); Compiler::emitVcpuAsm("LSLW", "", false); Compiler::emitVcpuAsm("LSLW", "", false); break;

                    default: break;
                }
            }

            Compiler::emitVcpuAsm("STW", Expression::byteToHexString(uint8_t(Compiler::getTempVarStart())), false);
        }

        return left;
    }

    Expression::Numeric LSR(Expression::Numeric& left, Expression::Numeric& right)
    {
        if(left._varType == Expression::Number  &&  right._varType == Expression::Number)
        {
            left._value = int16_t(std::lround(left._value)) >> int16_t(std::lround(right._value));
            return left;
        }

        Compiler::getNextTempVar();

        if((left._varType == Expression::TmpVar  ||  left._varType == Expression::IntVar)  &&  right._varType == Expression::Number)
        {
            // Optimised high byte read
            if(right._value == 8)
            {
                switch(left._varType)
                {
                    // Temporary variable address
                    case Expression::TmpVar:
                    {
                        Compiler::emitVcpuAsm("LD", Expression::byteToHexString(uint8_t(std::lround(left._value))) + " + 1", false);
                    }
                    break;

                    // User variable name
                    case Expression::IntVar:
                    {
                        int varIndex = Compiler::findVar(left._name);
                        if(varIndex == -1) fprintf(stderr, "Operator::LSR() : couldn't find variable name '%s'\n", left._name.c_str());
                        Compiler::emitVcpuAsm("LD", "_" + left._name + " + 1", false);
                    }
                    break;

                    default: break;
                }

                createTmpVar(left);
            }
            else
            {
                std::string opcode;
                switch(int16_t(std::lround(right._value)))
                {
                    case 1: opcode = "%ShiftRight1bit"; break;
                    case 2: opcode = "%ShiftRight2bit"; break;
                    case 3: opcode = "%ShiftRight3bit"; break;
                    case 4: opcode = "%ShiftRight4bit"; break;
                    case 5: opcode = "%ShiftRight5bit"; break;
                    case 6: opcode = "%ShiftRight6bit"; break;
                    case 7: opcode = "%ShiftRight7bit"; break;

                    default: break;
                }

                handleLogicalOp(opcode, left);
                Compiler::emitVcpuAsm(opcode, "", false);
            }
        }

        Compiler::emitVcpuAsm("STW", Expression::byteToHexString(uint8_t(Compiler::getTempVarStart())), false);

        return left;
    }

    Expression::Numeric ASR(Expression::Numeric& left, Expression::Numeric& right)
    {
        if(left._varType == Expression::Number  &&  right._varType == Expression::Number)
        {
            left._value /= (1 << int16_t(std::lround(right._value))) & 0x0000FFFF;
            return left;
        }

        Compiler::getNextTempVar();

        if((left._varType == Expression::TmpVar  ||  left._varType == Expression::IntVar)  &&  right._varType == Expression::Number)
        {
            std::string opcode;
            switch(int16_t(std::lround(right._value)))
            {
                case 1: opcode = "%ShiftRightSgn1bit"; break;
                case 2: opcode = "%ShiftRightSgn2bit"; break;
                case 3: opcode = "%ShiftRightSgn3bit"; break;
                case 4: opcode = "%ShiftRightSgn4bit"; break;
                case 5: opcode = "%ShiftRightSgn5bit"; break;
                case 6: opcode = "%ShiftRightSgn6bit"; break;
                case 7: opcode = "%ShiftRightSgn7bit"; break;
                case 8: opcode = "%ShiftRightSgn8bit"; break;

                default: break;
            }

            handleLogicalOp(opcode, left);
            Compiler::emitVcpuAsm(opcode, "", false);
        }

        Compiler::emitVcpuAsm("STW", Expression::byteToHexString(uint8_t(Compiler::getTempVarStart())), false);

        return left;
    }


    // ********************************************************************************************
    // Conditional Operators
    // ********************************************************************************************
    Expression::Numeric EQ(Expression::Numeric& left, Expression::Numeric& right, Expression::CCType ccType)
    {
        if(left._varType == Expression::Number  &&  right._varType == Expression::Number)
        {
            left._value = (left._value == right._value);
            return left;
        }

        bool invertedLogic = false;
        left._isValid = handleConditionOp(left, right, ccType, invertedLogic, "XOR");

        // Convert EQ into one of the condition types of branch instruction
        std::string cc = (ccType == Expression::FastCC) ? "Ne" : "Eq"; //(!invertedLogic) ? "Eq" : "Ne";
        emitCcType(ccType, cc);

        Compiler::emitVcpuAsm("STW", Expression::byteToHexString(uint8_t(Compiler::getTempVarStart())), false);

        return left;
    }

    Expression::Numeric NE(Expression::Numeric& left, Expression::Numeric& right, Expression::CCType ccType)
    {
        if(left._varType == Expression::Number  &&  right._varType == Expression::Number)
        {
            left._value = left._value != right._value;
            return left;
        }

        bool invertedLogic = false;
        left._isValid = handleConditionOp(left, right, ccType, invertedLogic, "XOR");

        // Convert NE into one of the condition types of branch instruction
        std::string cc = (ccType == Expression::FastCC) ? "Eq" : "Ne"; //(!invertedLogic) ? "Ne" : "Eq";
        emitCcType(ccType, cc);

        Compiler::emitVcpuAsm("STW", Expression::byteToHexString(uint8_t(Compiler::getTempVarStart())), false);

        return left;
    }

    Expression::Numeric LE(Expression::Numeric& left, Expression::Numeric& right, Expression::CCType ccType)
    {
        if(left._varType == Expression::Number  &&  right._varType == Expression::Number)
        {
            left._value = left._value <= right._value;
            return left;
        }

        bool invertedLogic = false;
        left._isValid = handleConditionOp(left, right, ccType, invertedLogic);

        // Convert LE into one of the condition types of branch instruction
        std::string cc = (!invertedLogic) ? "Le" : "Gt";
        emitCcType(ccType, cc);

        Compiler::emitVcpuAsm("STW", Expression::byteToHexString(uint8_t(Compiler::getTempVarStart())), false);

        return left;
    }

    Expression::Numeric GE(Expression::Numeric& left, Expression::Numeric& right, Expression::CCType ccType)
    {
        if(left._varType == Expression::Number  &&  right._varType == Expression::Number)
        {
            left._value = left._value >= right._value;
            return left;
        }

        bool invertedLogic = false;
        left._isValid = handleConditionOp(left, right, ccType, invertedLogic);

        // Convert GE into one of the condition types of branch instruction
        std::string cc = (!invertedLogic) ? "Ge" : "Lt";
        emitCcType(ccType, cc);

        Compiler::emitVcpuAsm("STW", Expression::byteToHexString(uint8_t(Compiler::getTempVarStart())), false);

        return left;
    }

    Expression::Numeric LT(Expression::Numeric& left, Expression::Numeric& right, Expression::CCType ccType)
    {
        if(left._varType == Expression::Number  &&  right._varType == Expression::Number)
        {
            left._value = (left._value < right._value);
            return left;
        }

        bool invertedLogic = false;
        left._isValid = handleConditionOp(left, right, ccType, invertedLogic);

        // Convert LT into one of the condition types of branch instruction
        std::string cc = (!invertedLogic) ? "Lt" : "Ge";
        emitCcType(ccType, cc);

        Compiler::emitVcpuAsm("STW", Expression::byteToHexString(uint8_t(Compiler::getTempVarStart())), false);

        return left;
    }

    Expression::Numeric GT(Expression::Numeric& left, Expression::Numeric& right, Expression::CCType ccType)
    {
        if(left._varType == Expression::Number  &&  right._varType == Expression::Number)
        {
            left._value = (left._value > right._value);
            return left;
        }

        bool invertedLogic = false;
        left._isValid = handleConditionOp(left, right, ccType, invertedLogic);

        // Convert GT into one of the condition types of branch instruction
        std::string cc = (!invertedLogic) ? "Gt" : "Le";
        emitCcType(ccType, cc);

        Compiler::emitVcpuAsm("STW", Expression::byteToHexString(uint8_t(Compiler::getTempVarStart())), false);

        return left;
    }


    // ********************************************************************************************
    // Math Operators
    // ********************************************************************************************
    Expression::Numeric POW(Expression::Numeric& left, Expression::Numeric& right)
    {
        if(left._varType == Expression::Number  &&  right._varType == Expression::Number)
        {
            left._value = pow(double(left._value), double(right._value));
            return left;
        }

        // Optimise base = 0
        if(left._varType == Expression::Number  &&  left._value == 0)
        {
            return Expression::Numeric(0, -1, true, false, false, Expression::Number, Expression::BooleanCC, Expression::Int16Both, std::string(""), std::string(""));
        }
        // Optimise base = 1
        else if(left._varType == Expression::Number  &&  left._value == 1)
        {
            return Expression::Numeric(1, -1, true, false, false, Expression::Number, Expression::BooleanCC, Expression::Int16Both, std::string(""), std::string(""));
        }
        // Optimise exponent = 0
        else if(right._varType == Expression::Number  &&  right._value == 0)
        {
            return Expression::Numeric(1, -1, true, false, false, Expression::Number, Expression::BooleanCC, Expression::Int16Both, std::string(""), std::string(""));
        }

        left._isValid = (Compiler::getCodeRomType() >= Cpu::ROMv5a) ? handleMathOp("CALLI", "power16bit", left, right) : handleMathOp("CALL", "power16bit", left, right);

        return left;
    }

    Expression::Numeric MUL(Expression::Numeric& left, Expression::Numeric& right)
    {
        if(left._varType == Expression::Number  &&  right._varType == Expression::Number)
        {
            left._value *= right._value;
            return left;
        }

        // Optimise multiply with 0
        if((left._varType == Expression::Number  &&  left._value == 0)  ||  (right._varType == Expression::Number  &&  right._value == 0))
        {
            return Expression::Numeric(0, -1, true, false, false, Expression::Number, Expression::BooleanCC, Expression::Int16Both, std::string(""), std::string(""));
        }

        left._isValid = (Compiler::getCodeRomType() >= Cpu::ROMv5a) ? handleMathOp("CALLI", "multiply16bit", left, right) : handleMathOp("CALL", "multiply16bit", left, right);

        return left;
    }

    Expression::Numeric DIV(Expression::Numeric& left, Expression::Numeric& right)
    {
        if(left._varType == Expression::Number  &&  right._varType == Expression::Number)
        {
            left._value = (right._value == 0) ? 0 : left._value / right._value;
            return left;
        }

        // Optimise divide with 0, term() never lets denominator = 0
        if((left._varType == Expression::Number  &&  left._value == 0)  ||  (right._varType == Expression::Number  &&  right._value == 0))
        {
            return Expression::Numeric(0, -1, true, false, false, Expression::Number, Expression::BooleanCC, Expression::Int16Both, std::string(""), std::string(""));
        }

        left._isValid = (Compiler::getCodeRomType() >= Cpu::ROMv5a) ? handleMathOp("CALLI", "divide16bit", left, right) : handleMathOp("CALL", "divide16bit", left, right);

        return left;
    }

    Expression::Numeric MOD(Expression::Numeric& left, Expression::Numeric& right)
    {
        if(left._varType == Expression::Number  &&  right._varType == Expression::Number)
        {
            left._value = (int16_t(std::lround(right._value)) == 0) ? 0 : int16_t(std::lround(left._value)) % int16_t(std::lround(right._value));
            return left;
        }

        // Optimise divide with 0, term() never lets denominator = 0
        if((left._varType == Expression::Number  &&  left._value == 0)  ||  (right._varType == Expression::Number  &&  right._value == 0))
        {
            return Expression::Numeric(0, -1, true, false, false, Expression::Number, Expression::BooleanCC, Expression::Int16Both, std::string(""), std::string(""));
        }

        left._isValid = (Compiler::getCodeRomType() >= Cpu::ROMv5a) ? handleMathOp("CALLI", "divide16bit", left, right, true) : handleMathOp("CALL", "divide16bit", left, right, true);

        return left;
    }
}