int main(int argc, char **argv);

void _start(int argc, char **argv) {
    int rc = main(argc, argv);
    extern void exit(int code);
    exit(rc);
}
