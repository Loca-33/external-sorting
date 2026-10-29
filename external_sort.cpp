#define _CRT_SECURE_NO_WARNINGS
#include "header.h"
#include <vector>
#include <queue>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <climits>
#include <cstdlib>
#include <functional>
#include <cstdio>
#include <map>
#include <ctime>  // 替换chrono为更通用的ctime

using namespace std;

// 常量定义
const size_t MEMORY_LIMIT = 2 * 1024 * 1024;  // 内存限制2MB
const int BUFFER_SIZE = 8192;                 // 缓冲区大小
const int DEFAULT_K_WAY = 3;                  // 默认K路归并数

// 排序策略枚举
enum SortStrategy {
    INTERNAL_SORT,
    REPLACEMENT_SELECTION
};

// 辅助函数声明
string generate_run_filename(int index);
void write_text_buffer(ofstream& out, vector<int>& buffer, int count);
vector<string> internal_sort_runs(ifstream& input, vector<long>& run_lengths);
vector<string> replacement_selection_runs(ifstream& input, vector<long>& run_lengths);
void k_way_merge(const vector<string>& input_files, const string& output_file, int k);
string build_merge_tree(vector<string>& run_files, vector<long>& run_lengths, int k_way);
void cleanup_temp_files(const vector<string>& files);
void log_performance(const string& strategy_name, unsigned long long memory_usage, double time_seconds);
void run_performance_comparison(ifstream& input);

// 生成归并段文件名
string generate_run_filename(int index) {
    return "run_" + to_string(index) + ".txt";
}

// 写入文本缓冲区辅助函数
void write_text_buffer(ofstream& out, vector<int>& buffer, int count) {
    static char num_str[16];  // 静态缓冲区减少内存分配
    for (int i = 0; i < count; i++) {
        int len = snprintf(num_str, sizeof(num_str), "%d\n", buffer[i]);
        out.write(num_str, len);
    }
}

// 内部排序产生初始归并段
vector<string> internal_sort_runs(ifstream& input, vector<long>& run_lengths) {
    vector<string> run_files;
    const size_t chunk_size = MEMORY_LIMIT / sizeof(int);
    vector<int> buffer;
    buffer.reserve(static_cast<int>(chunk_size));

    int value;
    int run_index = 0;

    while (true) {
        buffer.clear();

        // 读取一个内存块的数据
        while (buffer.size() < chunk_size && IntReader(input, value)) {
            buffer.push_back(value);
        }

        if (buffer.empty()) break;

        // 内部排序
        sort(buffer.begin(), buffer.end());

        // 写入归并段文件
        string filename = generate_run_filename(run_index++);
        ofstream out(filename, ios::binary);
        if (!out) {
            cerr << "Failed to open run file: " << filename << endl;
            continue;
        }

        write_text_buffer(out, buffer, static_cast<int>(buffer.size()));
        out.close();

        run_files.push_back(filename);
        run_lengths.push_back(static_cast<long>(buffer.size()));
    }

    return run_files;
}

// 置换选择算法产生初始归并段
vector<string> replacement_selection_runs(ifstream& input, vector<long>& run_lengths) {
    vector<string> run_files;
    const size_t heap_capacity = MEMORY_LIMIT / (sizeof(int) * 2);

    if (heap_capacity <= 0) {
        cerr << "Memory limit too small for sorting" << endl;
        return run_files;
    }

    vector<int> heap;
    heap.reserve(static_cast<int>(heap_capacity));
    vector<int> output_buffer(BUFFER_SIZE);
    vector<int> next_run;
    next_run.reserve(static_cast<int>(heap_capacity));
    int output_buffer_pos = 0;

    // 初始填充堆
    int value;
    while (heap.size() < heap_capacity && IntReader(input, value)) {
        heap.push_back(value);
    }

    if (heap.empty()) {
        return run_files;
    }

    make_heap(heap.begin(), heap.end(), greater<int>());

    int run_index = 0;
    while (!heap.empty() || !next_run.empty()) {
        string filename = generate_run_filename(run_index++);
        ofstream out(filename, ios::binary);
        if (!out) {
            cerr << "Failed to open run file: " << filename << endl;
            continue;
        }

        long run_length = 0;
        output_buffer_pos = 0;

        while (!heap.empty()) {
            // 获取堆顶元素
            pop_heap(heap.begin(), heap.end(), greater<int>());
            int min_val = heap.back();
            heap.pop_back();

            // 写入输出缓冲区
            output_buffer[output_buffer_pos++] = min_val;
            run_length++;

            if (output_buffer_pos == BUFFER_SIZE) {
                write_text_buffer(out, output_buffer, output_buffer_pos);
                output_buffer_pos = 0;
            }

            // 读取新元素
            int next_val;
            if (IntReader(input, next_val)) {
                if (next_val >= min_val) {
                    heap.push_back(next_val);
                    push_heap(heap.begin(), heap.end(), greater<int>());
                }
                else {
                    next_run.push_back(next_val);
                }
            }
        }

        // 刷新输出缓冲区
        if (output_buffer_pos > 0) {
            write_text_buffer(out, output_buffer, output_buffer_pos);
        }
        out.close();
        run_files.push_back(filename);
        run_lengths.push_back(run_length);

        // 准备下一归并段
        if (!next_run.empty()) {
            heap.swap(next_run);
            next_run.clear();
            make_heap(heap.begin(), heap.end(), greater<int>());
        }
    }

    return run_files;
}

// K路归并实现
void k_way_merge(const vector<string>& input_files, const string& output_file, int k) {
    k = min(k, static_cast<int>(input_files.size()));
    if (k == 0) return;

    vector<ifstream> input_streams(k);
    using MergeElement = pair<int, int>;
    priority_queue<MergeElement, vector<MergeElement>, greater<MergeElement>> pq;
    vector<int> current_values(k, INT_MAX);

    // 打开所有输入文件并读取第一个值
    for (int i = 0; i < k; i++) {
        input_streams[i].open(input_files[i], ios::binary);
        if (input_streams[i] && IntReader(input_streams[i], current_values[i])) {
            pq.push({ current_values[i], i });
        }
    }

    // 准备输出
    ofstream out(output_file, ios::binary);
    if (!out) {
        cerr << "Failed to open output file: " << output_file << endl;
        return;
    }

    vector<int> output_buffer(BUFFER_SIZE);
    int output_pos = 0;

    while (!pq.empty()) {
        auto min_element = pq.top();
        pq.pop();
        int min_val = min_element.first;
        int min_index = min_element.second;

        // 写入输出缓冲区
        output_buffer[output_pos++] = min_val;
        if (output_pos == BUFFER_SIZE) {
            write_text_buffer(out, output_buffer, output_pos);
            output_pos = 0;
        }

        // 从最小索引对应的文件中读取下一个值
        if (IntReader(input_streams[min_index], current_values[min_index])) {
            pq.push({ current_values[min_index], min_index });
        }
    }

    // 刷新输出缓冲区
    if (output_pos > 0) {
        write_text_buffer(out, output_buffer, output_pos);
    }

    // 关闭所有输入流
    for (auto& stream : input_streams) {
        if (stream.is_open()) {
            stream.close();
        }
    }
}

// 最佳归并树实现
string build_merge_tree(vector<string>& run_files, vector<long>& run_lengths, int k_way) {
    using Run = pair<long, string>;
    auto comp = [](const Run& a, const Run& b) { return a.first > b.first; };
    priority_queue<Run, vector<Run>, decltype(comp)> pq(comp);

    // 添加实际归并段
    for (size_t i = 0; i < run_files.size(); i++) {
        pq.push({ run_lengths[i], run_files[i] });
    }

    // 添加虚段（满足K叉哈夫曼树条件）
    int virtual_runs = 0;
    if (pq.size() > 1 && (pq.size() - 1) % (k_way - 1) != 0) {
        virtual_runs = (k_way - 1) - (pq.size() - 1) % (k_way - 1);
    }
    for (int i = 0; i < virtual_runs; i++) {
        pq.push({ 0, "" });
    }

    int merge_count = 0;
    while (pq.size() > 1) {
        vector<string> merge_files;
        vector<long> merge_lengths;
        long total_length = 0;

        // 选择K个归并段
        for (int i = 0; i < k_way && !pq.empty(); i++) {
            auto top = pq.top();
            pq.pop();
            if (!top.second.empty()) {
                merge_files.push_back(top.second);
                merge_lengths.push_back(top.first);
            }
            total_length += top.first;
        }

        // 执行K路归并
        if (!merge_files.empty()) {
            string new_file = "merge_" + to_string(merge_count++) + ".txt";
            k_way_merge(merge_files, new_file, k_way);
            pq.push({ total_length, new_file });
        }
    }

    return pq.empty() ? "sorted.txt" : pq.top().second;
}

// 清理临时文件
void cleanup_temp_files(const vector<string>& files) {
    for (const auto& file : files) {
        remove(file.c_str());
    }
}

// 记录性能数据
void log_performance(const string& strategy_name, unsigned long long memory_usage, double time_seconds) {
    ofstream logfile("performance.log", ios::app);
    if (logfile) {
        logfile << "Strategy: " << strategy_name << "\n";
        logfile << "Memory Usage: " << memory_usage / 1024.0 / 1024.0 << " MB\n";
        logfile << "Time: " << time_seconds << " seconds\n\n";
        logfile.close();
    }
}

// 运行性能比较
void run_performance_comparison(ifstream& input) {
    vector<pair<string, SortStrategy>> strategies = {
        {"Internal Sort + K-way Merge", INTERNAL_SORT},
        {"Replacement Selection + K-way Merge", REPLACEMENT_SELECTION}
    };

    vector<int> k_values = { 2, 3, 4, 5, 6, 8, 10, 12 };

    for (const auto& strategy : strategies) {
        for (int k : k_values) {
            // 重置输入流
            input.clear();
            input.seekg(0, ios::beg);

            // 记录开始时间
            clock_t start_time = clock();  // 使用clock()替代chrono

            // 设置环境变量
#ifdef _WIN32
            _putenv_s("SORT_STRATEGY", strategy.second == INTERNAL_SORT ? "INTERNAL" : "REPLACEMENT");
            _putenv_s("K_WAY", to_string(k).c_str());
#else
            setenv("SORT_STRATEGY", strategy.second == INTERNAL_SORT ? "INTERNAL" : "REPLACEMENT", 1);
            setenv("K_WAY", to_string(k).c_str(), 1);
#endif

            // 执行排序
            vector<long> run_lengths;
            vector<string> run_files;

            if (strategy.second == INTERNAL_SORT) {
                run_files = internal_sort_runs(input, run_lengths);
            }
            else {
                run_files = replacement_selection_runs(input, run_lengths);
            }

            if (!run_files.empty()) {
                string final_file = build_merge_tree(run_files, run_lengths, k);
                cleanup_temp_files(run_files);
            }

            // 记录结束时间和内存使用
            clock_t end_time = clock();
            double duration = (double)(end_time - start_time) / CLOCKS_PER_SEC;

            HANDLE handle = GetCurrentProcess();
            PROCESS_MEMORY_COUNTERS pmc;
            GetProcessMemoryInfo(handle, &pmc, sizeof(pmc));
            unsigned long long memory_usage = pmc.WorkingSetSize;

            // 记录性能数据
            string test_name = strategy.first + " (k=" + to_string(k) + ")";
            log_performance(test_name, memory_usage, duration);

            cerr << "Completed: " << test_name << endl;
            cerr << "  Memory: " << memory_usage / 1024.0 / 1024.0 << " MB" << endl;
            cerr << "  Time: " << duration << " seconds" << endl;
        }
    }
}

// 主排序函数
std::string ExternalSort(std::ifstream& input) {
    // 检查是否运行性能比较
    char* perf_compare = getenv("PERF_COMPARE");
    if (perf_compare != nullptr && string(perf_compare) == "1") {
        run_performance_comparison(input);
        return "sorted.txt";  // 性能比较模式下直接返回，不实际排序
    }

   
    SortStrategy strategy = REPLACEMENT_SELECTION;
    int k_way = DEFAULT_K_WAY;

    // 检查环境变量以确定排序策略
    char* strategy_env = getenv("SORT_STRATEGY");
    if (strategy_env != nullptr) {
        string strategy_str(strategy_env);
        if (strategy_str == "INTERNAL") {
            strategy = INTERNAL_SORT;
        }
    }

    // 检查环境变量以确定k值
    char* k_way_env = getenv("K_WAY");
    if (k_way_env != nullptr) {
        k_way = atoi(k_way_env);
        if (k_way < 2) {
            k_way = DEFAULT_K_WAY;
        }
    }

    vector<long> run_lengths;
    vector<string> run_files;

    // 根据策略选择不同的初始归并段生成方法
    if (strategy == INTERNAL_SORT) {
        cerr << "Using internal sort for initial runs..." << endl;
        run_files = internal_sort_runs(input, run_lengths);
    }
    else {
        cerr << "Using replacement selection for initial runs..." << endl;
        run_files = replacement_selection_runs(input, run_lengths);
    }

    cerr << "Generated " << run_files.size() << " initial runs" << endl;
    cerr << "Using " << k_way << "-way merge..." << endl;

    if (run_files.empty()) {
        ofstream out("sorted.txt");
        out.close();
        return "sorted.txt";
    }

    string final_file = build_merge_tree(run_files, run_lengths, k_way);

    // 清理临时文件
    cleanup_temp_files(run_files);

    return final_file.empty() ? "sorted.txt" : final_file;
}
