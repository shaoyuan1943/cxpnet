#include "cxpnet/ensure.h"

#include <iostream>
#include <string>
#include <vector>

void check_user_profile(int user_id, const std::string& name) {
  std::cout << "Checking profile for user ID: " << user_id << ", Name: " << name << std::endl;

  // 使用你的 ENSURE 宏，它现在支持格式化字符串！
  ENSURE(user_id > 0, "User ID must be a positive number, but got {}.", user_id);
  ENSURE(!name.empty(), "User name cannot be empty for ID {}.", user_id);

  std::cout << "Profile check passed." << std::endl;
}

int main() {
  std::cout << "--- Running cxpnet examples ---" << std::endl;

  std::cout << "\n[Test Case 1: Valid Profile]" << std::endl;
  check_user_profile(101, "Alice");

  std::cout << "\n[Test Case 2: Invalid User ID]" << std::endl;
  check_user_profile(-5, "Bob");

  std::cout << "\n[Test Case 3: Empty Name]" << std::endl;
  check_user_profile(102, "");

  std::cout << "\n--- Example finished ---" << std::endl;

  return 0;
}