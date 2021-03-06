#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <list>
#include <map>
#include <string>
#include <vector>

class AssemblerSimple_SingleInstruction_Test;

#include "assembler.h"
#include "instruction_encoder.h"
#include "logger.h"
#include "tokens.h"
#include "../common/printer.h"
#include "parser_gen/parser.hpp"
#include "../thirdparty/jsonxx/jsonxx.h"

Assembler::Assembler(bool log_enable, utils::Printer & printer)
{
    this->logger = new AssemblerLogger(printer);
    this->log_enable = log_enable;
    this->encoder = new InstructionEncoder(log_enable, printer);
}

bool Assembler::processInstruction(std::string const & filename, Token const * inst, uint32_t & encoded_instruction) const
{
    std::string const & op = inst->str;
    bool success = true;
    std::list<Instruction *> const & encs = encoder->insts[op];

    Instruction * candidate_match = nullptr;
    bool found_candidate = false;
    bool found_match = false;

    // check all encodings to see if there is a match
    for(Instruction * enc : encs) {
        // first make sure the number of operands is the same, otherwise it's a waste
        if(enc->oper_types.size() == (uint32_t) inst->num_operands) {
            candidate_match = enc;
            found_candidate = true;
            bool actual_match = true;
            Token const * cur_oper = inst->opers;

            // iterate through the oeprand types to see if the assembly matches
            for(Operand const * oper : candidate_match->oper_types) {
                if(! oper->compareTypes(cur_oper->type)) {
                    actual_match = false;
                    break;
                }

                cur_oper = cur_oper->next;
            }

            // found a match, stop searching
            if(actual_match) {
                found_match = true;
                break;
            }
        }
    }

    if(! found_match) {
        // if there was no match, check to see if it was because of incorrect number of operands or incorrect operands
        if(! found_candidate) {
            // this will only be the case if there are no encodings with the same number of operands as the assembly line
            if(log_enable) {
                logger->printfMessage(utils::PrintType::ERROR, filename, inst, file_buffer[inst->row_num], "incorrect number of operands for instruction \'%s\'", inst->str.c_str());
            }
        } else {
            // this will only be the case if there is at least one encoding with the same number of operands as the assembly line
            // since there is still no match, this will assume you were trying to match against the last encoding in the list
            const Token *cur = inst->opers;

            // iterate through the assembly line to see which operands were incorrect and print errors
            if(log_enable) {
                for(auto it = candidate_match->oper_types.begin(); it != candidate_match->oper_types.end(); it++) {
                    if(! (*it)->compareTypes(cur->type)) {
                        logger->printfMessage(utils::PrintType::ERROR, filename, cur, file_buffer[inst->row_num], "incorrect operand");
                    }

                    cur = cur->next;
                }
            }
        }

        success = false;
    } else {
        // there was a match, so take that match and encode
        success &= encoder->encodeInstruction(log_enable, *logger, candidate_match, inst, encoded_instruction);
    }

    return success;
}

// note: new_orig is untouched if the .orig is not valid
bool Assembler::setOrig(std::string const & filename, Token const * orig, int & new_orig)
{
    if(orig->checkPseudoType("orig")) {     // sanity check...
        if(orig->num_operands != 1) {
            if(log_enable) {
                logger->printfMessage(utils::PrintType::WARNING, filename, orig, file_buffer[orig->row_num], "incorrect number of operands");
            }
            return false;
        } else {
            if(orig->opers->type != NUM) {
                if(log_enable) {
                    logger->printfMessage(utils::PrintType::WARNING, filename, orig->opers, file_buffer[orig->row_num], "illegal operand");
                }
                return false;
            } else {
                new_orig = orig->opers->num;
                return true;
            }
        }
    }

    return true;
}

bool Assembler::processPseudo(std::string const & filename, Token const * pseudo)
{
    if(pseudo->checkPseudoType("orig")) {
        int dummy;
        setOrig(filename, pseudo, dummy);
    } else if(pseudo->checkPseudoType("end")) {
        // do nothing
    }

    return true;
}

Token * Assembler::removeNewlineTokens(Token * program)
{
    Token * prev_state = nullptr;
    Token * cur_state = program;

    // remove newline tokens
    while(cur_state != nullptr) {
        bool delete_cur_state = false;
        if(cur_state->type == NEWLINE) {
            if(prev_state) {
                prev_state->next = cur_state->next;
            } else {
                // if we start off with newlines, move the program pointer forward
                program = cur_state->next;
            }
            delete_cur_state = true;
        } else {
            prev_state = cur_state;
        }

        Token * next_state = cur_state->next;
        if(delete_cur_state) {
            delete cur_state;
        }
        cur_state = next_state;
    }

    // since you may have moved the program pointer, you need to return it
    return program;
}

bool Assembler::processTokens(std::string const & filename, Token * program,  Token *& program_start)
{
    program = removeNewlineTokens(program);

    Token * cur_state = program;
    bool found_valid_orig = false;
    int cur_orig = 0;

    // find the orig
    while(cur_state != nullptr && ! found_valid_orig) {
        // move through the program until you find the first orig
        while(cur_state != nullptr && ! cur_state->checkPseudoType("orig")) {
            // TODO: allow for exceptions, such as .external
            if(log_enable) {
                logger->xprintfMessage(  utils::PrintType::WARNING, filename, 0, file_buffer[cur_state->row_num].length(), cur_state
                                       , file_buffer[cur_state->row_num], "ignoring statement before valid .orig");
            }
            cur_state = cur_state->next;
        }

        // looks like you hit nullptr before a .orig, meaning there is no .orig
        if(cur_state == nullptr) {
            if(log_enable) {
                logger->printf(utils::PrintType::ERROR, "no .orig found in \'%s\'", filename.c_str());
            }
            return false;
        }

        // check to see if .orig is valid
        // if so, stop looking; if not, move on and try again
        if(setOrig(filename, cur_state, cur_orig)) {
            found_valid_orig = true;
        }

        cur_state = cur_state->next;
    }

    // you hit nullptr after seeing at least one .orig, meaning there is no valid .orig
    if(! found_valid_orig) {
        if(log_enable) {
            logger->printf(utils::PrintType::ERROR, "no valid .orig found in \'%s\'", filename.c_str());
        }
        return false;
    }

    // write output
    program_start = cur_state;

    int pc_offset = 0;
    while(cur_state != nullptr) {
        if(cur_state->checkPseudoType("orig")) {
            if(! setOrig(filename, cur_state, cur_orig)) {
                if(log_enable) {
                    logger->printf(utils::PrintType::WARNING, "ignoring invalid .orig");
                }
            } else {
                pc_offset = 0;
            }
        }

        if(cur_state->type == LABEL) {
            std::string const & label = cur_state->str;

            if(log_enable) {
                logger->printf(utils::PrintType::DEBUG, "setting label \'%s\' to 0x%X", label.c_str(), cur_orig + pc_offset);
            }
        }

        cur_state->pc = pc_offset;

        if(cur_state->type == INST) {
            pc_offset += 1;

            Token * oper = cur_state->opers;
            while(oper != nullptr) {
                if(oper->type == STRING) {
                    if(encoder->findReg(oper->str)) {
                        oper->type = OPER_TYPE_REG;
                    } else {
                        oper->type = OPER_TYPE_LABEL;
                    }
                } else if(oper->type == NUM) {
                    oper->type = OPER_TYPE_UNTYPED_NUM;
                }

                oper = oper->next;
            }
        }
        // TODO: account for block allocations (e.g. .fill, .stringz)

        cur_state = cur_state->next;
    }

    return true;
}

bool Assembler::assembleProgram(std::string const & filename, Token * program)
{
    std::ifstream file(filename);
    bool success = true;

    // load program into buffer for error messages
    if(file.is_open()) {
        std::string line;

        file_buffer.clear();
        while(std::getline(file, line)) {
            file_buffer.push_back(line);
        }

        file.close();
    } else {
        if(log_enable) {
            logger->printf(utils::PrintType::WARNING, "somehow the file got destroyed in the last couple of ms, skipping file %s ...", filename.c_str());
        }
        return false;
    }

    if(log_enable) {
        logger->printf(utils::PrintType::INFO, "beginning first pass ...");
    }

    Token * cur_state = nullptr;
    if(! processTokens(filename, program, cur_state)) {
        if(log_enable) {
            logger->printf(utils::PrintType::ERROR, "first pass failed");
        }
        return false;
    }

    if(log_enable) {
        logger->printf(utils::PrintType::INFO, "first pass completed successfully, beginning second pass ...");
    }

    while(cur_state != nullptr) {
        if(cur_state->checkPseudoType("orig")) {
            success &= processPseudo(filename, cur_state);
        }

        if(cur_state->type == INST) {
            uint32_t encoded_instruction;

            success &= processInstruction(filename, cur_state, encoded_instruction);
        }
        cur_state = cur_state->next;
    }

    if(log_enable) {
        if(success) {
            logger->printf(utils::PrintType::INFO, "second pass completed successfully");
        } else {
            logger->printf(utils::PrintType::ERROR, "second pass failed");
        }
    }

    return success;
}

extern FILE *yyin;
extern int yyparse(void);
extern Token *root;
extern int row_num, col_num;

void Assembler::genObjectFile(char const * filename)
{
    std::map<std::string, int> symbol_table;
    if((yyin = fopen(filename, "r")) == nullptr) {
         // printer.printf(utils::PrintType::WARNING, "skipping file %s ...", filename);
    } else {
        row_num = 0; col_num = 0;
        yyparse();
        assembleProgram(filename, root);

        fclose(yyin);
    }
}
