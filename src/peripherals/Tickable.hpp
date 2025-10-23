#pragma once

class Tickable {
public:
    virtual ~Tickable() = default;
    virtual void tick() = 0;
};