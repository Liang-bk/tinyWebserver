#include <iostream>

#include "buffer/buffer.h"
#include <unistd.h>
#include <fcntl.h>
// TIP To <b>Run</b> code, press <shortcut actionId="Run"/> or
// click the <icon src="AllIcons.Actions.Execute"/> icon in the gutter.
int main() {
    // TIP Press <shortcut actionId="RenameElement"/> when your caret is at the
    // <b>lang</b> variable name to see how CLion can help you rename it.
    Buffer buffer;
    std::string s1 = "hello world!";
    std::string s2 = " another string";
    buffer.append(s1);
    assert(buffer.readableBytes() == s1.size());
    assert(buffer.writableBytes() == 1024 - s1.size());
    buffer.append(s2);
    assert(*buffer.peek() == 'h');
    assert(buffer.prependableBytes() == 0);
    assert(buffer.readableBytes() == s1.size() + s2.size());
    assert(buffer.writableBytes() == 1024 - s1.size() - s2.size());
    buffer.retrieve(6);
    buffer.retrieveUntil(buffer.peek() + sizeof(char) * 6);
    assert(*buffer.peek() == ' ');
    assert(buffer.prependableBytes() == s1.size());
    assert(buffer.readableBytes() == s2.size());
    assert(buffer.writableBytes() == 1024 - s1.size() - s2.size());
    assert(buffer.retrieveAllAsString() == s2);

    int fd = open("test.txt", O_RDONLY, 0);
    buffer.readFd(fd, &errno);
    assert(buffer.readableBytes() == 1118);


    return 0;
}

// TIP See CLion help at <a
// href="https://www.jetbrains.com/help/clion/">jetbrains.com/help/clion/</a>.
//  Also, you can try interactive lessons for CLion by selecting
//  'Help | Learn IDE Features' from the main menu.