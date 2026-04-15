#ifndef RUNTIME_APP_H
#define RUNTIME_APP_H

class RuntimeApp {
public:
    // Boot the standalone runtime executable and stay in the traditional game
    // loop until the engine requests quit.
    int Run();
};

#endif
