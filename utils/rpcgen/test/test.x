typedef struct foo* foolist;
struct foo {
    string bar<>;
    foolist next;
};

union bar switch (int baz) {
case 0:
    foo x;
case 1:
    int y;
default:
    void;
};

struct writereq {
    opaqueref buf<>;
};

program TEST {
    version TEST_1 {
        void TEST_NULL(void) = 0;
        int TEST_ECHO(int) = 1;
        foolist TEST_LIST(void) = 2;
        bar TEST_GETBAR(void) = 3;
        int TEST_WRITE(writereq) = 4;
    } = 1;
} = 1234;
