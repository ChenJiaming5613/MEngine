#pragma once

#include "Application.hpp"

#include <memory>

namespace mengine {

std::unique_ptr<Application> CreateViewerApp();

} // namespace mengine
