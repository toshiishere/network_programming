#include <stdio.h>
#include <string.h>

int main() {
    char myString[] = "hello world";
    char searchChar = ' ';
    char *result;

    result = strchr(myString, searchChar);

    if (result != NULL) {
        printf("Character '%c' found at position: %s\n", searchChar, result+1);
    } else {
        printf("Character '%c' not found in the string.\n", searchChar);
    }

    return 0;
}