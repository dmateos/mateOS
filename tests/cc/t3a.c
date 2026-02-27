int print(const char *s);
int helper(void);

int main(void) {
    if (helper() != 42)
        return 3;
    print("multi-file ok\n");
    return 0;
}
