#ifndef PEREGRINE_SRC_INTERNAL_CODING_STYLE_H_
#define PEREGRINE_SRC_INTERNAL_CODING_STYLE_H_

namespace peregrine::coding_style {

// Assumptions about coding conventions.
// ---------------------------------------------------------------------------
//
// We follow Google' https://google.github.io/styleguide/cppguide.html style.
// Beyond that, we also add a few to help make our code easier to read.
//
// When reading code, we often want to find a class's non-static data members
// (not functions) quickly: given all the data members, it is easy to guess
// what the class might do, greatly reducing the code-reading time. Therefore,
// we organize class header files in the following public/private blocks:
//
//     class ClassName {
//      public:   // The first block has all the public interfaces.
//        .....
//      private:
//        .....
//      private:  // The middle private blocks group logically different code
//        .....   // into separate groups.
//      private:
//        .....
//      private:  // The last private block always contains __all_and_only__
//        .....   // the non-static data members.
//     };
//
// This way, the reader can choose to collapse a class header file into blocks
// and quickly find the last private block for all the non-static data members,
// often in one screen. If the data members span more than one screen, it may
// be a good time to refactor the code.
inline constexpr bool kClassLastPrivateBlockHasAllNonStaticDataMembers = true;

// Class member function names: public/protected functions start with uppercase
// (FunctionName), and private functions start with lowercase (functionName),
// an naming convention from the Go language. All other function names start
// with uppercase. This way, by just looking at a function name, we can tell
// if it's private or not, saving our code reading time.
inline constexpr bool kClassPrivateFunctionNamesStartWithLowercase = true;

}  // namespace peregrine::coding_style

#endif  // PEREGRINE_SRC_INTERNAL_CODING_STYLE_H_
