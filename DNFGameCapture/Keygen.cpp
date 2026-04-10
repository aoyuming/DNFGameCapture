#include <iostream>
#include <string>
#include <ctime>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <fstream>

using namespace std;

unsigned int SimpleHash(const string& str) {
    unsigned int hash = 5381;
    for (char c : str) { hash = ((hash << 5) + hash) + c; }
    return hash;
}

string GetDurationLabel(int choice, int customValue) {
    switch (choice) {
    case 1: return "测试卡 (1天)";
    case 2: return "月卡 (30天)";
    case 3: return "季卡 (90天)";
    case 4: return "年卡 (365天)";
    case 5: return "永久卡 (永不过期)";
    case 6: { char buf[64]; snprintf(buf, sizeof(buf), "自定义 (%d天)", customValue); return string(buf); }
    case 7: { char buf[64]; snprintf(buf, sizeof(buf), "自定义 (%d分钟)", customValue); return string(buf); }
    }
    return "未知";
}

int main() {
    srand((unsigned int)time(NULL));
    system("color 0B");
    cout << "====================================================" << endl;
    cout << "   DNF 统计工具 - CDK激活码算号器 (激活开始倒计时)" << endl;
    cout << "====================================================" << endl;

    while (true) {
        cout << "\n请选择要生成的卡密类型：" << endl;
        cout << "1. 测试卡 (1天)\n2. 月卡   (30天)\n3. 季卡   (90天)\n4. 年卡   (365天)" << endl;
        cout << "5. 永久卡 (永不过期)\n6. 自定义 [天数]\n7. 自定义 [分钟] (开发压力测试专用)\n0. 退出程序" << endl;
        cout << "\n请选择 (0-7): ";

        int choice;
        if (!(cin >> choice)) { cin.clear(); cin.ignore(10000, '\n'); continue; }
        if (choice == 0) break;
        if (choice < 1 || choice > 7) { cout << "❌ 无效选择！" << endl; continue; }

        long long durationSec = 0;
        int customValue = 0;
        switch (choice) {
        case 1: durationSec = 1 * 24 * 3600; break;
        case 2: durationSec = 30 * 24 * 3600; break;
        case 3: durationSec = 90 * 24 * 3600; break;
        case 4: durationSec = 365 * 24 * 3600; break;
        case 5: durationSec = 0xFFFFFFFF; break; // 永久卡标记
        case 6: { cout << "请输入天数: "; cin >> customValue; durationSec = (long long)customValue * 24 * 3600; break; }
        case 7: { cout << "请输入分钟数: "; cin >> customValue; durationSec = (long long)customValue * 60; break; }
        }

        cout << "请输入要生成的卡密个数: ";
        int count = 1; cin >> count;
        if (count < 1) count = 1; if (count > 10000) count = 10000;

        string durationLabel = GetDurationLabel(choice, customValue);

        ofstream fout("秘钥(CDK版).txt", ios::app);
        if (!fout.is_open()) { cout << "❌ 无法打开 秘钥.txt 进行写入！" << endl; continue; }

        time_t batchNow = time(nullptr); tm bt; localtime_s(&bt, &batchNow);
        char batchHeader[128];
        snprintf(batchHeader, sizeof(batchHeader),
            "\r\n========== [%04d-%02d-%02d %02d:%02d:%02d] 批次生成 %d 张 [%s] ==========\r\n",
            bt.tm_year + 1900, bt.tm_mon + 1, bt.tm_mday, bt.tm_hour, bt.tm_min, bt.tm_sec, count, durationLabel.c_str());
        fout << batchHeader;
        fout << "卡密状态: 未激活 (用户首次使用时开始计算时间)\r\n----------------------------------------------------\r\n";

        cout << "\n----------------------------------------------------" << endl;
        for (int i = 1; i <= count; i++) {
            int randomNonce = rand() % 0xFFFF;
            char signBuf[256];
            // 【核心修改】：签名数据使用 durationSec 而不是绝对时间
            snprintf(signBuf, sizeof(signBuf), "%llX-%04X-MySuperSecretKey2026", durationSec, randomNonce);
            unsigned int signature = SimpleHash(string(signBuf));

            char keyBuf[256];
            // 【核心修改】：前缀改为 CDK，用于和老卡密区分
            snprintf(keyBuf, sizeof(keyBuf), "CDK-%llX-%04X-%08X", durationSec, randomNonce, signature);

            printf("[%04d/%04d] %s\n", i, count, keyBuf);
            char lineBuf[512]; snprintf(lineBuf, sizeof(lineBuf), "[%04d] %s\r\n", i, keyBuf);
            fout << lineBuf;
        }

        fout << "========== 本批次结束 ==========\r\n"; fout.close();
        cout << "----------------------------------------------------" << endl;
        cout << "✅ 全部生成成功！共 " << count << " 张" << endl;
        cout << "📌 模式: " << durationLabel << " (首次激活后才开始倒计时)" << endl;
        cout << "----------------------------------------------------" << endl;
    }
    return 0;
}