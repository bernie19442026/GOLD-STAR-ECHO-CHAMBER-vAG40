#pragma once

#include "standalone_app.h"

class StandaloneUI {
public:
    StandaloneUI(StandaloneApp* app);
    ~StandaloneUI();
    void runEventLoop();

private:
    StandaloneApp* app_;
};
