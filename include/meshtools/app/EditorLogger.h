#pragma once

#include <string>
#include <vector>

namespace meshtools::app {

class EditorLogger {
  public:
    void append(std::string origin, std::string message);
    void replace(std::vector<std::string> messages);

    [[nodiscard]] const std::vector<std::string>& messages() const;

  private:
    std::vector<std::string> messages_;
};

}  // namespace meshtools::app
