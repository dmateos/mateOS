int libtiny_value(void);
int print(const char *s);

int main(void) {
    if (libtiny_value() != 7)
        return 4;
    print("archive ok\n");
    return 0;
}
