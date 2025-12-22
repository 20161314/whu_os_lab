uint32 strlen(const char* str) {
    int count = 0; //创建计数器
    while (*str != '\0') { //对 str 解引用，如果 *str 不是 \0
        str++; // 指针向后移动1位（char）
        count++; // 计数器+1
    }
    return count; //返回计数器
}

void* memcpy(void* dst, const void* src, uint32 n) {
    char* d = dst;
    const char* s = src;
    while (n--) {
        *d++ = *s++;
    }
    return dst;
}
