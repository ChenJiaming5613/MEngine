#pragma once

#include "Application.h"

#include <memory>

namespace mengine {

std::unique_ptr<Application> CreateViewerApp();

} // namespace mengine
