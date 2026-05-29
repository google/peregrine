#ifndef PEREGRINE_SRC_UTIL_MACRO_H_
#define PEREGRINE_SRC_UTIL_MACRO_H_

#define __ACTION_ON_COPY(ClassName, ACTION) \
  ClassName(const ClassName&) = ACTION;     \
  ClassName& operator=(const ClassName&) = ACTION

#define __ACTION_ON_MOVE(ClassName, ACTION) \
  ClassName(ClassName&&) = ACTION;          \
  ClassName& operator=(ClassName&&) = ACTION

// Allows copy constructor/assignment for a class.
#define ALLOW_COPY(ClassName) __ACTION_ON_COPY(ClassName, default)

// Disallows copy constructor/assignment for a class.
#define DISALLOW_COPY(ClassName) __ACTION_ON_COPY(ClassName, delete)

// Allows move constructor/assignment for a class.
#define ALLOW_MOVE(ClassName) __ACTION_ON_MOVE(ClassName, default)

// Disallows move constructor/assignment for a class.
#define DISALLOW_MOVE(ClassName) __ACTION_ON_MOVE(ClassName, delete)

#endif  // PEREGRINE_SRC_UTIL_MACRO_H_
