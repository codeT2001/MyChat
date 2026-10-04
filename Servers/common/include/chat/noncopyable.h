#ifndef _NONCOPYABLE_H_
#define _NONCOPYABLE_H_

// 禁止拷贝与移动构造/赋值，配合 C++11 Meyers 单例使用。
// 用法：在类的 public 区写 DISALLOW_COPY_MOVE(ClassName);
#define DISALLOW_COPY_MOVE(ClassName)                  \
    ClassName(const ClassName&) = delete;              \
    ClassName& operator=(const ClassName&) = delete;   \
    ClassName(ClassName&&) = delete;                    \
    ClassName& operator=(ClassName&&) = delete

#endif // _NONCOPYABLE_H_
