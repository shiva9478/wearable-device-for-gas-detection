#include <Arduino.h>
#include <unity.h>

void test_hello_world() {
    Serial.print("Hello, World!");
    TEST_ASSERT_EQUAL_STRING("Hello, World!", "Hello, World!");
}

void setup() {
    UNITY_BEGIN();
    test_hello_world();
    UNITY_END();
}

void loop() {
}