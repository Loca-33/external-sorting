#pragma once
#include <iostream>
#include <fstream>
#include <string>

// 添加 Windows API 支持
#define NOMINMAX
#include <Windows.h>
#include <Psapi.h>

#include <string>
#include <fstream>

// 函数声明
std::string ExternalSort(std::ifstream& input);
bool IntReader(std::ifstream& in, int& num);



std::string ExternalSort(std::ifstream &input);
bool IntReader(std::ifstream &input, int &num);
