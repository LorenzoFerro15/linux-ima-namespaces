#include "ima.h"

void print_util(unsigned char* to_be_printed, unsigned int length, char* string_before)
{
    char string_to_print[1024];
    int offset=0;

    for (int i = 0; i < length; i++)
    {
        sprintf(string_to_print+offset, "%02hhX", to_be_printed[i]);
        offset+=2;
    }

    printk(KERN_DEBUG "%s %s",string_before, string_to_print);
    
}