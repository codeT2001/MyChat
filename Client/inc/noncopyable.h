#ifndef _NONCOPYABLE_H_
#define _NONCOPYABLE_H_

// 禁用拷贝与移动构造/赋值的工具宏
// 用法：在类的 private 区使用 DISALLOW_COPY_MOVE(ClassName)
#define DISALLOW_COPY_MOVE(ClassName)                 \
    ClassName(const ClassName &) = delete;            \
    ClassName(ClassName &&) = delete;                 \
    ClassName &operator=(const ClassName &) = delete; \
    ClassName &operator=(ClassName &&) = delete;

#endif // _NONCOPYABLE_H_
