/*
 * IPv4 (with optional port) extractor.
 *
 * Rules (all hand-rolled, no atoi/strtol/sscanf/inet_XXX/regex):
 *   - A "candidate token" is a maximal run of characters drawn only from
 *     {0-9, '.', ':'}. Any other character (letters, spaces, punctuation,
 *     etc.) is a separator and breaks the token.
 *   - Exactly one candidate token per line may match the full grammar:
 *         octet.octet.octet.octet[:port]
 *     where each octet is 1-3 digits, value 0-255, no leading zero unless
 *     the octet is exactly "0"; port (if present) is 1-5 digits, value
 *     0-65535, same leading-zero rule.
 *   - A token must match the grammar EXACTLY end-to-end. Any extra
 *     character left over (a stray trailing '.', a second ':', a colon
 *     that isn't immediately after the 4th octet, etc.) fails that whole
 *     token -- there is no partial/sub-token extraction.
 *   - The first token in the line that fully validates is the answer.
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* A character that can be part of an IPv4[:port] candidate token. */
static int isCandidateChar(char c) {
    return isdigit((unsigned char)c) || c == '.' || c == ':'; /* checks for valid address characters */
}

/*
 * Parse one octet starting at tok[*j]. Advances *j past the digits it
 * consumed (only on success is the advance meaningful/used further).
 * Returns 1 on success (value in *value), 0 on failure.
 */
static int parseOctet(const char* tok, size_t len, size_t* j, int* value) {
    size_t start = *j; /* saves where this octet starts */
    int digits = 0; /* counts the digits in the octet */
    int val = 0; /* stores the octet value */

    while (*j < len && isdigit((unsigned char)tok[*j])) {
        val = val * 10 + (tok[*j] - '0'); /* builds the number */
        digits++; /* counts this digit */
        (*j)++; /* moves to the next character */
        if (digits > 3) {
            return 0; /* too many digits for a valid octet */
        }
    }

    if (digits == 0) {
        return 0; /* empty octet */
    }
    if (digits > 1 && tok[start] == '0') {
        return 0; /* disallowed leading zero, e.g. "01" */
    }
    if (val > 255) {
        return 0; /* out of range */
    }

    *value = val; /* returns the valid octet */
    return 1;
}

/*
 * Parse a port starting at tok[*j]. Same conventions as parseOctet.
 */
static int parsePort(const char* tok, size_t len, size_t* j, int* value) {
    size_t start = *j; /* saves where the port starts */
    int digits = 0; /* counts the port digits */
    long val = 0; /* stores the port value */

    while (*j < len && isdigit((unsigned char)tok[*j])) {
        val = val * 10 + (tok[*j] - '0'); /* builds the port number */
        digits++; /* counts this digit */
        (*j)++; /* moves to the next character */
        if (digits > 5) {
            return 0; /* too many digits for a valid port */
        }
    }

    if (digits == 0) {
        return 0; /* empty port after ':' */
    }
    if (digits > 1 && tok[start] == '0') {
        return 0; /* disallowed leading zero */
    }
    if (val > 65535) {
        return 0; /* out of range */
    }

    *value = (int)val; /* returns the valid port */
    return 1;
}

/*
 * Attempt to match the ENTIRE token [tok, tok+len) against
 * octet.octet.octet.octet[:port]. Returns 1 on success.
 */
static int tryParseToken(const char* tok, size_t len, unsigned long* outAddr, int* outPort) {
    size_t j = 0; /* current position in the token */
    int octets[4]; /* stores the four address parts */
    int k; /* counts the address parts */

    for (k = 0; k < 4; k++) {
        if (!parseOctet(tok, len, &j, &octets[k])) {
            return 0;
        }
        if (k < 3) {
            if (j >= len || tok[j] != '.') {
                return 0; /* missing separator -> wrong octet count */
            }
            j++; /* moves past the period */
        }
    }

    int port = -1; /* -1 means no port was found */
    if (j < len) {
        if (tok[j] != ':') {
            /* leftover char right after the 4th octet that isn't ':'
               (e.g. a stray trailing '.') -> whole token is invalid */
            return 0;
        }
        j++; /* moves past the colon */
        if (!parsePort(tok, len, &j, &port)) {
            return 0;
        }
    }

    if (j != len) {
        /* anything left over (second ':', trailing junk, etc.) fails */
        return 0;
    }

    *outAddr = ((unsigned long)octets[0] << 24) |
               ((unsigned long)octets[1] << 16) |
               ((unsigned long)octets[2] << 8)  |
               ((unsigned long)octets[3]); /* combines the address parts */
    *outPort = port; /* returns the port value */
    return 1;
}

/*
 * Returns 1 if a valid address was found, 0 otherwise.
 * On success: *outAddress holds the 32-bit value, and
 * *outPort holds the port number, or -1 if no port was present.
 * On failure: *outAddress is set to 0 and *outPort is set to -1.
 */
int extractIPv4(const char* str, unsigned long* outAddress, int* outPort) {
    *outAddress = 0; /* clears the address result */
    *outPort = -1; /* starts with no port */

    size_t len = strlen(str); /* gets the input length */
    size_t i = 0; /* starts at the first character */

    while (i < len) {
        if (!isCandidateChar(str[i])) {
            i++;
            continue;
        }

        size_t start = i; /* saves the start of this token */
        while (i < len && isCandidateChar(str[i])) {
            i++;
        }
        size_t tokLen = i - start; /* gets the token length */

        unsigned long addr;
        int port;
        if (tryParseToken(str + start, tokLen, &addr, &port)) {
            *outAddress = addr;
            *outPort = port;
            return 1; /* stops at the first valid address */
        }
        /* token failed validation entirely -> treat as garbage, keep scanning */
    }

    return 0;
}

/* Strip trailing \n and \r (handles both Unix and Windows line endings). */
static void trimNewline(char* s) {
    size_t len = strlen(s); /* gets the line length */
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) {
        s[len - 1] = '\0'; /* removes the line ending */
        len--; /* updates the length */
    }
}

/* Case-insensitive comparison of s (after trimming surrounding
   spaces/tabs) against target. Used only for the quit command. */
static int equalsIgnoreCaseTrimmed(const char* s, const char* target) {
    while (*s == ' ' || *s == '\t') {
        s++; /* skips spaces at the start */
    }
    size_t len = strlen(s); /* gets the trimmed length */
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t')) {
        len--; /* ignores spaces at the end */
    }
    size_t tlen = strlen(target); /* gets the target length */
    if (len != tlen) {
        return 0;
    }
    for (size_t i = 0; i < len; i++) {
        char a = s[i];
        char b = target[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
        if (a != b) {
            return 0;
        }
    }
    return 1;
}

int main(void) {
    char line[1024]; /* stores one line of input */

    while (1) {
        printf("Enter a string (or 'END' to quit): ");
        fflush(stdout); /* shows the prompt right away */

        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            break; /* EOF */
        }
        trimNewline(line); /* removes enter from the input */

        if (equalsIgnoreCaseTrimmed(line, "end")) {
            printf("Program terminated.\n");
            break;
        }

        unsigned long addr; /* stores the address result */
        int port; /* stores the port result */
        if (extractIPv4(line, &addr, &port)) {
            unsigned int a = (unsigned int)((addr >> 24) & 0xFF); /* gets the first part */
            unsigned int b = (unsigned int)((addr >> 16) & 0xFF); /* gets the second part */
            unsigned int c = (unsigned int)((addr >> 8) & 0xFF); /* gets the third part */
            unsigned int d = (unsigned int)(addr & 0xFF); /* gets the last part */

            if (port == -1) {
                printf("Extracted IPv4 address: %u.%u.%u.%u (decimal value: %lu, port: none)\n",
                       a, b, c, d, addr);
            } else {
                printf("Extracted IPv4 address: %u.%u.%u.%u (decimal value: %lu, port: %d)\n",
                       a, b, c, d, addr, port);
            }
        } else {
            printf("Invalid input: no valid IPv4 address found\n");
        }
    }

    return 0;
}