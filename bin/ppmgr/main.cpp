#include <windows.h>
#include <tlhelp32.h>
#include <pdh.h>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <unordered_map>

#pragma comment(lib, "pdh.lib")

struct Process {
    DWORD pid;
    std::string name;
    float cpu;
    SIZE_T ram;
    bool efficiency;
};

std::vector<Process> GetProcesses() {
    std::vector<Process> result;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe = { sizeof(pe) };
    if (Process32FirstW(hSnap, &pe)) {
        do {
            std::wstring wname(pe.szExeFile);
            std::string name(wname.begin(), wname.end());
            result.push_back({ pe.th32ProcessID, name, 0.0f, 0, false });
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return result;
}

PDH_HQUERY cpuQuery;
std::unordered_map<DWORD, PDH_HCOUNTER> cpuCounters;

void InitCPU() {
    PdhOpenQuery(NULL, 0, &cpuQuery);
}

void AddProcessCounter(DWORD pid) {
    wchar_t counterPath[256];
    swprintf(counterPath, 256, L"\\Process(%d)\\%% Processor Time", pid);
    PdhAddCounterW(cpuQuery, counterPath, 0, &cpuCounters[pid]);
}

float GetCPUForProcess(DWORD pid) {
    PDH_FMT_COUNTERVALUE value;
    PdhCollectQueryData(cpuQuery);
    if (cpuCounters.find(pid) == cpuCounters.end()) {
        AddProcessCounter(pid);
    }
    PdhGetFormattedCounterValue(cpuCounters[pid], PDH_FMT_DOUBLE, NULL, &value);
    return static_cast<float>(value.doubleValue);
}

int main() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    SetConsoleOutputCP(CP_UTF8);

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hOut, &csbi);
    DWORD mode;
    GetConsoleMode(hIn, &mode);
    SetConsoleMode(hIn, mode & ~(ENABLE_MOUSE_INPUT | ENABLE_WINDOW_INPUT));
    FlushConsoleInputBuffer(hIn);

    InitCPU();
    PdhCollectQueryData(cpuQuery);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    std::vector<Process> processes;
    int selected = 0;
    int metric = 0;
    const char* metrics[] = {"CPU", "RAM"};
    int printedLines = 0;

    INPUT_RECORD ir;
    DWORD events;

    while (true) {
        processes = GetProcesses();
        GetConsoleScreenBufferInfo(hOut, &csbi);
        int maxLines = csbi.srWindow.Bottom - csbi.srWindow.Top - 2;

        // Clear previous frame
        COORD clearPos = {0, (SHORT)(csbi.dwCursorPosition.Y - printedLines)};
        for (int i = 0; i < printedLines; ++i) {
            SetConsoleCursorPosition(hOut, {clearPos.X, (SHORT)(clearPos.Y + i)});
            printf("%*s", csbi.dwSize.X, "");
        }
        SetConsoleCursorPosition(hOut, {0, (SHORT)(csbi.dwCursorPosition.Y - printedLines)});
        printedLines = 0;

        printf("Process Monitor [Tab: Cycle] | %s\n", metrics[metric]); printedLines++;
        printf("------------------------------------------------\n"); printedLines++;

        for (int i = 0; i < processes.size() && i < maxLines; ++i) {
            auto& p = processes[i];
            p.cpu = GetCPUForProcess(p.pid);
            float usage = (metric == 0) ? p.cpu : p.ram / 1024.0f / 1024.0f;

            WORD color = FOREGROUND_GREEN;
            if (usage > 70) color = FOREGROUND_RED;
            else if (usage > 30) color = FOREGROUND_RED | FOREGROUND_GREEN;

            if (i == selected) color |= BACKGROUND_BLUE;

            SetConsoleTextAttribute(hOut, color);
            printf("%-20s ⚡ %5.1f%%\n", p.name.c_str(), usage); printedLines++;
        }

        SetConsoleTextAttribute(hOut, csbi.wAttributes);

        if (PeekConsoleInput(hIn, &ir, 1, &events)) {
            ReadConsoleInput(hIn, &ir, 1, &events);
            if (ir.EventType == KEY_EVENT && ir.Event.KeyEvent.bKeyDown) {
                auto& key = ir.Event.KeyEvent;
                if (key.wVirtualKeyCode == VK_TAB) metric = !metric;
                else if (key.wVirtualKeyCode == VK_UP && selected > 0) selected--;
                else if (key.wVirtualKeyCode == VK_DOWN && selected < processes.size()-1) selected++;
                else if (key.wVirtualKeyCode == VK_ESCAPE) break;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    // Final clear
    SetConsoleCursorPosition(hOut, {0, (SHORT)(csbi.dwCursorPosition.Y - printedLines)});
    for (int i = 0; i < printedLines; ++i) {
        printf("\n");
    }

    PdhCloseQuery(cpuQuery);
    return 0;
}   