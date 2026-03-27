#import <Foundation/Foundation.h>
#include "standalone_app.h"
#include "standalone_ui.h"
#include <iostream>

int main(int /*argc*/, const char* /*argv*/[]) {
    @autoreleasepool {
        std::cout << "Gold Star Echo Chamber vAG40" << std::endl;
        std::cout << "============================" << std::endl;

        StandaloneApp app;

        if (!app.initialize()) {
            std::cerr << "Failed to initialize audio engine" << std::endl;
            return 1;
        }

        auto irs = app.getAvailableIRs();
        std::cout << "Available impulse responses:" << std::endl;
        for (size_t i = 0; i < irs.size(); ++i) {
            std::cout << "  " << i << ": " << irs[i] << std::endl;
        }

        app.run();

        StandaloneUI ui(&app);
        ui.runEventLoop();

        app.shutdown();
        std::cout << "Goodbye." << std::endl;
    }
    return 0;
}
