/**
* Copyright 2020 (C) Hailo Technologies Ltd.
* All rights reserved.
*
* Hailo Technologies Ltd. ("Hailo") disclaims any warranties, including, but not limited to,
* the implied warranties of merchantability and fitness for a particular purpose.
* This software is provided on an "AS IS" basis, and Hailo has no obligation to provide maintenance,
* support, updates, enhancements, or modifications.
*
* You may use this software in the development of any project.
* You shall not reproduce, modify or distribute this software without prior written permission.
**/
/**
* @file c_basic_parser.h
* @brief Basic parser
*
* Parses configuration file as c-string input with a terminating 0.
**/
#ifndef _DIS_C_BASIC_PARSER_
#define _DIS_C_BASIC_PARSER_

#include "common.h"

#include <stdlib.h>
#include <cstring>
#include <string>

/// @brief finds the sub-string 'name' and the first number after it, no matter whether it is in the same line
/// or commented.
/// @param cfg_str configuration file in c-string format
/// @param name the string of the parameter name to be found in cfg_str
static const char* find_param(const char* cfg_str, const char* const name)
{
    if( !cfg_str || !name //||(strlen(name) < 1)
        ){
        return  NULL;
    }
    const char* pc = cfg_str;
    do {
        pc = strstr(pc, name);  //find the searched param name
        if(pc == NULL){ //if not found
            return NULL;
        }
        //search for a comment '#' before the name in this line
        const char* lstart = pc;
        while ((lstart > cfg_str) && (*lstart != 0x0A)) { //sweep back to previous new line or start
            if(*lstart == '#'){//comment found before the name in same line
                break;
            }
            lstart--;
        }
        char prev = ' ';
        if(pc > cfg_str){
            prev = *(pc-1);
        }
        //---------------------
        pc += strlen(name); //move to after the name
        if(*lstart == '#'){ //comment found before the name in same line - continue search
            continue;
        }
        if((prev != ' ') && (prev != '\t') && (prev != '\n') && (prev != '\r') ){
            //check the character before the name. If is not a white space, 'name' is a sub-string of a longer name
            continue;
        }
        if((*pc != ' ') && (*pc != '\t') && (*pc != '=') && (*pc != ':') && (*pc != ',') && (*pc != '|') ){
            //check the character after the name. If is not a white space,'=',':',',','|',
            //then 'name' is a sub-string of a longer name
            continue;
        }
        //The name found, not commented
        //find first digit
        while((*pc!=0) && ((*pc<'0')||(*pc>'9')) &&(*pc!='-')&&(*pc!='+')&&(*pc!='.'))
            pc++;
        if(*pc==0){ //end found, no digit
            return NULL;
        }
        // a digit found
        return pc;
    } while (true);
}

/// @brief finds the sub-string 'name' and the first number after it, no matter whether it is in the same line
/// or commented.
/// @param cfg_str configuration file in c-string format
/// @param name the string of the parameter name to be found in cfg_str
// static std::string find_string_param(const char* cfg_str, const char* const name)
// {
//     if( !cfg_str || !name //||(strlen(name) < 1)
//         ){
//         return  "";
//     }
//     const char* pc = cfg_str;
//     do {
//         pc = strstr(pc, name);  //find the searched for param name
//         if(pc == NULL){ //if not found
//             return "";
//         }
//         //search for a comment '#' before the name in this line
//         const char* lstart = pc;
//         while ((lstart > cfg_str) && (*lstart != 0x0A)) { //sweep back to previous new line or start
//             if(*lstart == '#'){//comment found before the name in same line
//                 break;
//             }
//             lstart--;
//         }
//         char prev = ' ';
//         if(pc > cfg_str){
//             prev = *(pc-1);
//         }
//         //--------------------
//         pc += strlen(name); //move to after the name
//         if(*lstart == '#'){ //comment found before the name in same line - continue search
//             continue;
//         }
//         if((prev != ' ') && (prev != '\t') && (prev != '\n') && (prev != '\r') ){
//             //check the character before the name. If is not a white space, 'name' is a sub-string of a longer name
//             continue;
//         }
//         if((*pc != ' ') && (*pc != '\t') && (*pc != '=') && (*pc != ':') && (*pc != ',') && (*pc != '|') ){
//             //check the character after the name. If is not a white space,'=',':',',','|',
//             //then 'name' is a sub-string of a longer name
//             continue;
//         }
//         //The name found, not commented

//         // skip " = "
//         while (*pc == ' ' || *pc == '=')
//         {
//             pc++;
//         }

//         // check why we stopped - is it end of line/file?
//         if (*pc == 0 || *pc == '\n' || *pc == '\r')
//         {
//             return "";
//         }

//         // pc points to a valid value - now find where it ends
//         const char * pend = pc;
//         // go to end of line or file
//         while (*pend != 0 && *pend != '\r' && *pend != '\n')
//         {
//             pend++;
//         }
//         pend--;

//         // scroll back the end pointer - trim spaces
//         while (pend > pc && *pend == ' ')
//         {
//             pend--;
//         }

//         // trim just one quote
//         if (*pend == '"')
//         {
//             pend--;
//         }

//         // scroll forward the start pointer - just once
//         if (*pc == '"')
//         {
//             pc++;
//         }

//         const int val_length = pend - pc + 1;
//         if (val_length < 1)
//         {
//             return "";
//         }
//         return std::string(pc, val_length);

//         return pc;
//     } while (true);
// }

/// @param parse_str configuration file in c-string format
/// @param name the string of the parameter name to be found in cfg_str
/// @param count_read count
static float read_float(const  char* parse_str, const char* name , int& count_read) {
    const char* num_str = find_param(parse_str, name );
    if(num_str) {
        count_read=1;
        return  (float)atof(num_str);
    }else {
        count_read=0;
        return  (float)0;
    }
}

/// @param parse_str configuration file in c-string format
/// @param name the string of the parameter name to be found in cfg_str
/// @param count_read count
static int read_int( const char* parse_str, const char* name , int& count_read ) {
    const char* num_str = find_param(parse_str, name );
    if(num_str) {
        count_read=1;
        return atoi(num_str);
    }else {
        count_read=0;
        return 0;
    }
}

/// @param parse_str configuration file in c-string format
/// @param name the string of the parameter name to be found in cfg_str
/// @param count_read count
/// @param out output array
/// @param size output array size
// static void read_float_array(const char* parse_str, const char* name  , int& count_read,
//                              float* out, int size) {
//     const char* num_str = find_param(parse_str, name ); //find param name

//     count_read=0;
//     if(num_str) for(int ai=0; ai<size; ai++) {    //loop for array elements
//         while((*num_str!=0) && ((*num_str<'0')||(*num_str>'9')) &&(*num_str!='-')&&(*num_str!='+')&&(*num_str!='.'))
//             num_str++; //find next start of a number
//         if(num_str){  //read the number
//             out[ai] = (float)atof(num_str); count_read++;
//         }
//         while(((*num_str>='0')&&(*num_str<='9')) ||(*num_str=='-')||(*num_str=='+')||(*num_str=='.')||(*num_str=='e'))
//             num_str++;//move after the number
//     }
// }

/// @param parse_str configuration file in c-string format
/// @param name the string of the parameter name to be found in cfg_str
/// @param count_read count
// static std::string READ_STRING( const char* parse_str, const char* name , int& count_read  ) {
//     std::string candidate = find_string_param(parse_str, name);
//     if (candidate == "") {
//         count_read = 0;
//     } else {
//         count_read = 1;
//     }
//     return candidate;
// }

#endif // _DIS_C_BASIC_PARSER_
