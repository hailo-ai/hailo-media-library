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
* @file log.h
* @brief DIS logging functionalities
*
* Enables logging of error messages by printf as well as to a file by defining the corresponding macro
* (either LOG_PRINTF or LOG_FILE). Contains an option for more verbose logging by defining LOG_DEBUG.
**/
#ifndef _DIS_LOG_H_
#define _DIS_LOG_H_

#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdarg>
#include <mutex>
#include <chrono>


#define LOG_TAG "[DIS] "

/// Define to enable logs for debug. Comment to leave only logs for critical errors
#define LOG_DEBUG

/// ONLY ONE of the following should be defined!
/// Log by printf
#define LOG_PRINTF

// Log to dis_log_file.txt
// #define LOG_FILE


#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)


#if defined(LOG_PRINTF)
    #define LOGE(a, ...) do { printf(LOG_TAG a, ##__VA_ARGS__); printf("\n"); fflush(stdout); } while(false)
    #ifdef LOG_DEBUG
        #define LOG(a, ...) do { printf(LOG_TAG a, ##__VA_ARGS__); printf("\n"); fflush(stdout); } while(false)
    #else
        #define LOG(a, ...) do { } while(false)
    #endif

#elif defined(LOG_FILE)
    // LOG_TO_FILE uses fprinf on all systems (including QNX)

    #define LOG_FILENAME "dis_log_file.txt"

    class DisFileLog{
    public:
        DisFileLog() : DisFileLog(LOG_FILENAME) {}
        DisFileLog(const char * _fname)
        {
            fname = fopen(_fname, "w");
            if (fname == NULL) fprintf(stderr, "Error opening log file %s\n", _fname);
        }
        ~DisFileLog()
        {
            if (fname != NULL)
            {
                int close = fclose(fname);
                if (close == EOF) fprintf(stderr, "Error closing log file\n");
            }
        }
        FILE * getFD() {return fname;}
    private:
        FILE* fname = NULL;
    };

    extern DisFileLog disFileLog;
    #define LOGE(a, ...) do { fprintf(disFileLog.getFD(), LOG_TAG a, ##__VA_ARGS__); fprintf(disFileLog.getFD(), "\n");\
    fflush(disFileLog.getFD()); } while(false)
    #ifdef LOG_DEBUG
        #define LOG(a, ...) do { fprintf(disFileLog.getFD(), LOG_TAG a, ##__VA_ARGS__); \
        fprintf(disFileLog.getFD(), "\n"); fflush(disFileLog.getFD()); } while(false)
    #else
        #define LOG(a, ...) do { } while(false)
    #endif

#elif //no logs

#define LOG(...)
#define LOGE(...)

#endif //LOG_TO_FILE

#define DIS_ABORT(a, ...) do { LOGE("[Error][%s:%d] " a "\n", __FILENAME__, __LINE__, ##__VA_ARGS__); \
fflush(stdout); abort(); } while(false)

#endif /* _DIS_LOG_H_ */
