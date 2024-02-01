Purpose of this guide is to have consistent code style at a project level, because consistent code style is easy to read and to follow. **We optimize for readability.**

If, even after reading this guide you find yourself in doubt, feel free to consult [Google C++ Guide](https://google.github.io/styleguide/cppguide.html) and try to adopt a reasonable convention.

## Spacing rules

* Tab size is 4 spaces, enable "Editor: Insert spaces" in VSCode
* Maximum line length should be kept at 120 characters
* If the function arguments do not all fit on one line, they should be broken up onto multiple lines, with each subsequent line aligned with the first argument
```
    void do_something(int first_param, int second_param,
                      int third_param, int fourth_param);

    void when_not_even_first_parameter_can_fit_into_this_line(
        int first_param,
        int second_param,
        int third_param,
        int fourth_param);
```
* Function calls should follow the same rule as for function declarations
```
Valid choices:
do_something(very_long_arg_name,
             another_arg);
do_something(
    very_long_arg_name, another_arg);

Invalid:
do_something(very_long_arg_name,
    another_arg);
```
* If an expression must be split into multiple lines, each subsequent line shoud be aligned with the start of the expression
```
Valid choices:
    int some_random_sum = var_1 + var_2 + var_3 +
                          very_long_function_name(var_1, var_2, var_3);

    int some_random_sum =
        var_1 + var_2 + var_3 + very_long_function_name(var_1, var_2, var_3);

Invalid:
    int some_random_sum = var_1 + var_2 +
        var_3 + very_long_function_name(var_1, var_2, var_3);
```
* In conditonals end line with boolean operators if needed as in
```
    if (cond_a && cond_b &&
        cond_c)
    {

    }
```
* Feel free to add extra parentheses if it would make an expression more readable
* Namespace content should be indented
```
namespace pipegen2
{
    class A
    {
        // ...
    };
}
```
* Opening parentheses "{" should go into new line
* Use parentheses "{}" even for one line statements
```
    if (phase_count > 2)
    {
        return true;
    }
```
* Do not put more than one statement or declare more than one variable in the single line

## Ordering rules

* Public methods come first, then protected then private methods and members, with a blank line separating each group
* Constructors/destructors come first, then methods, then static member variables, then other member variables
```
public:
    // Constructs A from initial count.
    A(int count);

    // Destructs A.
    ~A();

protected:
    // Does some fun.
    void do_fun();

private:
    // Does some private fun.
    void do_private_fun();

    // Prefix used to print fun.
    static int c_fun_prefix;

    // Counts amount of fun.
    int m_count;
```
* Actual order of initialization in constructors is the same as the order in which corresponding member variables are defined in the class, not the order in which member initializations are defined in constructor itself, so please try to keep them the same to avoid confusion.
```
// --- WRONG ---
    class A
    {
    public:
        A(int count) :
            // m_name will actually be initialized before m_count because that's the order those members are defined in
            // the class, so it will be initialized with the string of some random value of m_count.
            m_count(count),
            m_name(std::to_string(m_count))
        {
        }

    private:
        std::string m_name;

        int m_count;
    };

// +++ Correct +++
    class A
    {
    public:
        A(int count) :
            m_name(std::to_string(count)),
            m_count(count)
        {
        }

    private:
        std::string m_name;

        int m_count;
    };
```

## Naming rules

* Use snake_case for file, namespace, function and variable names
* Use PascalCase for class, structure, enum names
* Use m_ prefix for non public member variables, s_ for static variables, c_ for constants
```
// File: thread_counter.cpp
namespace pipegen2
{
    class ThreadCounter
    {
    public:
        enum Type
        {
            Alpha = 0,
            Beta
        };

        void do_something();

    private:
        int m_thread_count;
    };
}
```
* Do not declare shorthand names for class pointers or types like `using BufferMap = std::unordered_map<NodeId, const BufferNode*>`. Doing this will save a few characters when writing code, but it also takes a few brain cycles when reading code, because reader has to do mapping in their head each time they come across the shorthand name (Intellisense is not always working or available, for example when doing code review). It can also be ambiguous, for example you can't deduce from BufferMap name if it is a map of BufferNode references or pointers, not to mention const. Use full types everywhere, or _auto_ where appropriate.
* It is appropriate to use _auto_ only for long and complex types which are not really important for the context of the code to the person reading it (for example cumbersome iterator types in a for loop).

## Includes order

Includes should be ordered in following groups, each group alphabetically sorted and separated by an empty line:
1. .h/.hpp header of this .cpp file (if .cpp file)
2. Standard library headers
3. Imported libraries headers
4. This project headers
```
// File: stream_creator.cpp
#include "stream_creator.h"

#include <algorithm>
#include <string>
#include <vector>

#include "imports/boost/tr1/memory.hpp"

#include "base_stream_node.h"
#include "phase.h"
```

## Comments

* Strive to make code readable by itself and avoid excessive comments. Comments are ignored by compilers and so they can be ignored by developers when maintaining the code as well. As a result, a comment that is very old may contain out-of-date information, which can be worse than no information at all. Avoid disinformation.
* Comments that tell you **what** the code is doing are an indication that the code is not clear enough: the methods are too weakly cohesive, and/or are not named expressively (named to reveal their intent).
* Comments that tell you **why** the code is doing what it is doing are helpful, because they express what the code cannot: arbitrary business rules.
* Do not leave commented code around without appropriate "TODO:" comment as a reminder to remove/uncomment it later.
* Almost every function declaration should have header comments immediately preceding it that describe what the function does and how to use it. These comments may be omitted only if the function is simple and obvious (e.g. simple accessors for obvious properties of the class). Private methods are not exempt.
* Function header comments should be written with an implied starting phrase "This function ...", and should start with the verb phrase. For example, "Opens the file" rather than "Open the file".
* Each member variable of class in header files should have appropriate header comment explaining what it is used for, except when it is very obvious.

## Header files extension
File extension of newly created header files should be **.h**.
This is a pure convention for uniformity of the newly written code, i.e. there is no special reason in this project for choosing .h over .hpp since we don't need interoperability between C and C++.
Therefore, there is no need for renaming the existing .hpp files and refactoring the existing code.
