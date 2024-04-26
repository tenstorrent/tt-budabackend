Purpose of this guide is to have consistent code style at a project level, because consistent code style is easy to read and to follow. **We optimize for readability.**

## Spacing rules

* Tab size is 4 spaces, enable "Editor: Insert spaces" in VSCode
* Maximum line length should be kept at 120 characters
* If the function arguments do not all fit on one line, they should be broken up onto multiple lines, with each subsequent line aligned with the first argument.
```
    void do_something(int first_param, int second_param,
                      int third_param, int fourth_param);

    void when_not_even_first_parameter_can_fit_into_this_line(
        int first_param,
        int second_param,
        int third_param,
        int fourth_param);
```
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
* Each member of class in header files should have appropriate comment explaining what it is/does, except when it is very obvious
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

    // Counts amount of fun.
    int m_count;
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

## Header files extension
File extension of newly created header files should be **.h**.
This is a pure convention for uniformity of the newly written code, i.e. there is no special reason in this project for choosing .h over .hpp since we don't need interoperability between C and C++.
Therefore, there is no need for renaming the existing .hpp files and refactoring the existing code.
