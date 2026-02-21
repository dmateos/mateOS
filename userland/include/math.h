#ifndef MATEOS_MATH_H
#define MATEOS_MATH_H

double strtod(const char *nptr, char **endptr);
float strtof(const char *nptr, char **endptr);
long double strtold(const char *nptr, char **endptr);
long double ldexpl(long double x, int exp);

#endif
