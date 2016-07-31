#ifndef CONSTANTS_H_   /* Include guard */
#define CONSTANTS_H_

#define SERVER_PORT 15000
#define INITIAL_SIZE 32
#define OFFSET_MAX 4294967295
#define FAILURE -1
#define SUCCESS 1
#define DOUBLE_MIN -9999999999
#define SKIPLIST_MAX_LEVEL 16

#define HASH_LEN 4

#define SIMPLE_STRING 0
#define RESP_ERROR 1
#define RESP_INTEGER 2
#define BULKSTRING 3
#define RESP_ARRAY 4

#define EXOCMD 101
#define BITMAP 102
#define SORTED_SET 103

// ERRORS

#define COMMAND_NOT_FOUND -1
#define WRONG_NUMBER_OF_ARGUMENTS -2
#define WRONG_TYPE_OF_COMMAND_ON_TARGET_OBJECT -3
#define PROTOCOL_ERROR -4
#define FAILED_TO_FLUSH_DB -5
#define OFFSET_NOT_INT_OR_OUT_OF_RANGE -6
#define BIT_NOT_INT_OR_OUT_OF_RANGE -7
#define INCR_OPTION_SUPPORTS_A_SINGLE_INCREMENT_ELEMENT_PAIR -8
#define SYNTAX_ERROR -9
#define VALUE_IS_NOT_A_VALID_FLOAT -10
#define XX_AND_NX_OPTIONS_AT_THE_SAME_TIME_ARE_NOT_COMPATIBLE -11
#define MIN_OR_MAX_IS_NOT_A_FLOAT -12


// LOGGING
#define RED   "\x1B[31m"
#define GRN   "\x1B[32m"
#define YEL   "\x1B[33m"
#define BLU   "\x1B[34m"
#define MAG   "\x1B[35m"
#define CYN   "\x1B[36m"
#define WHT   "\x1B[37m"
#define RESET "\x1B[0m"


#endif