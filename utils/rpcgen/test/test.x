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

program TEST {
    version TEST_1 {
        void TEST_NULL(void) = 0;
        int TEST_ECHO(int) = 1;
        foolist TEST_LIST(void) = 2;
    } = 1;
} = 1234;
