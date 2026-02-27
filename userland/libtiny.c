// Export as $libtiny_value so SmallerC output can resolve it.
int libtiny_value_alias(void) __asm__("$libtiny_value");
int libtiny_value_alias(void) { return 7; }
