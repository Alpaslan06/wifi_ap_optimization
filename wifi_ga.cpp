#include <iostream>
#include <vector>
#include <tuple>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <random>
#include <cstring>      // strcpy, strtok, strlen
#include <cstdio>       // FILE*, fopen, fgets, fprintf, fclose
#include <getopt.h>     // getopt_long
#include <sqlite3.h>    // SQLite3
#include <pthread.h>    // pthread
#include <unistd.h>     // sleep, system
#include <ncurses.h>    // ncurses
#ifdef USE_OPENCV
#include <opencv2/opencv.hpp>  // OpenCV
#endif
#include "httplib.h"    // httplib.h (cpp-httplib)

using namespace std;

// ------------------------------------------------------
// Temel Yapılar ve Global Değişkenler
// ------------------------------------------------------

struct AP {
    int x, y, kanal;
    char label[32];
    double talep;  // Zafiyet: Uninitialized kullanılabilir
};

vector<AP> kullanicilar;         // Burada kullanıcı listesi, zafiyetler için
vector<AP> en_iyi_birey;
double en_iyi_skor = -1e9;

int AP_SAYISI = 0;
double globalOrtalamaFitness = 0.0;
bool dur = false;
sqlite3* db = nullptr;
string configDosya = "config.txt";

// Random generator
random_device rd;
mt19937 gen(rd());
uniform_real_distribution<> rand01(0.0, 1.0);

int randint(int min, int max) {
    uniform_int_distribution<> dis(min, max - 1);
    return dis(gen);
}

// ------------------------------------------------------
// Zafiyet Test Fonksiyonları
// ------------------------------------------------------

// 🔥 Null pointer dereference zafiyeti
void test_null_pointer() {
    int* ptr = nullptr;
    if (randint(0, 20) == 7) {
        *ptr = 10;  // Crash edebilir
    }
}

// 🔥 Uninitialized variable zafiyeti
int belirsiz_kullan() {
    int x;               // Başlatılmadı
    return x + 1;        // Undefined behavior
}

// 🔥 Race condition zafiyeti: iki thread aynı dosyaya yazıyor
void* raceConditionThread(void* arg) {
    while (!dur) {
        FILE* f = fopen("race_log.txt", "a");
        if (f) {
            fprintf(f, "Thread ID: %lu Logging...\n", pthread_self());
            // Filesimultaneous write, sync yok
            fclose(f);
        }
        sleep(1);
    }
    return nullptr;
}

// ------------------------------------------------------
// Dosya I/O: Konfig Okuma & Optimal Yerleşim Kaydetme
// ------------------------------------------------------

void konfigDosyasiniOku(const char* dosyaAdi) {
    FILE* fp = fopen(dosyaAdi, "r");   // fopen NULL kontrolü yok
    if (!fp) {
        return;
    }
    char satir[64];                    // Buffer overflow potansiyeli
    while (fgets(satir, 64, fp) != nullptr) {
        char* token = strtok(satir, ",");
        if (!token) continue;
        int x = atoi(token);

        token = strtok(nullptr, ",");
        if (!token) continue;
        int y = atoi(token);

        token = strtok(nullptr, "\n");
        if (!token) continue;
        double t = atof(token);

        AP k;
        k.x = x; k.y = y; k.kanal = randint(1, 14); k.talep = t;
        // 🔥 strcpy overflow riski
        snprintf(k.label, sizeof(k.label), "%d_%d", x, y);
        kullanicilar.push_back(k);
    }
    fclose(fp);
}

void kaydetOptimalYerlesim(const vector<AP>& optimal) {
    FILE* fout = fopen("optimal.txt", "w");  // fopen dönüş kontrolü yok
    if (!fout) return;
    for (size_t i = 0; i < optimal.size(); i++) {
        fprintf(fout, "AP #%zu: x=%d y=%d kanal=%d label=%s talep=%.2f\n", 
                i, optimal[i].x, optimal[i].y, optimal[i].kanal, optimal[i].label, optimal[i].talep);
    }

    // 🔥 Use-After-Free zafiyeti
    char* tehlikeliPtr = (char*)malloc(20);
    strcpy(tehlikeliPtr, "zafiyet_test");
    free(tehlikeliPtr);
    fprintf(fout, "Use-after-free: %s\n", tehlikeliPtr);

    fclose(fout);
}

// ------------------------------------------------------
// SQLite Veritabanı İşlemleri
// ------------------------------------------------------

void veritabaniAc(const char* dbAdi) {
    int rc = sqlite3_open(dbAdi, &db);
    if (rc != SQLITE_OK) {
        db = nullptr;
    }
}

void veritabaniKapat() {
    if (db) sqlite3_close(db);
}

void veritabaniyeYaz(const vector<AP>& optimal) {
    if (!db) return;
    const char* createSQL = 
        "CREATE TABLE IF NOT EXISTS yerlesim ("
        "ap_id INTEGER, x INTEGER, y INTEGER, kanal INTEGER, label TEXT, talep REAL);";
    char* hataMesaji = nullptr;
    sqlite3_exec(db, createSQL, nullptr, 0, &hataMesaji);
    if (hataMesaji) sqlite3_free(hataMesaji);

    // 🔥 SQL Injection riski: label doğrudan query içinde
    for (size_t i = 0; i < optimal.size(); i++) {
        char sql[512];  // potansiyel buffer overflow
        snprintf(sql, sizeof(sql),
                 "INSERT INTO yerlesim (ap_id, x, y, kanal, label, talep) "
                 "VALUES (%zu, %d, %d, %d, '%s', %.2f);",
                 i, optimal[i].x, optimal[i].y, optimal[i].kanal, optimal[i].label, optimal[i].talep);
        hataMesaji = nullptr;
        sqlite3_exec(db, sql, nullptr, 0, &hataMesaji);
        if (hataMesaji) sqlite3_free(hataMesaji);
    }
}

// ------------------------------------------------------
// Ağ Zamanı Simülasyonu (Integer Overflow Potansiyeli)
// ------------------------------------------------------

int agZamaniSimulasyonu(int paketBoyutu) {
    int hesap = paketBoyutu * 1024;    // overflow riski
    if (hesap > 100000) {
        return 5;
    }
    return hesap / 100;
}

// ------------------------------------------------------
// Genetik Algoritma: AP Dizisi ve Rastgele Birey Oluşturma
// ------------------------------------------------------

vector<AP> rastgele_birey() {
    vector<AP> birey;
    for (int i = 0; i < AP_SAYISI; i++) {
        AP ap;
        ap.x = randint(0, 100);
        ap.y = randint(0, 100);
        ap.kanal = randint(1, 14);
        ap.talep = rand01(gen) * 10;
        // 🔥 strcpy overflow potansiyeli
        snprintf(ap.label, sizeof(ap.label), "AP_%d_%d", ap.x, ap.y);
        birey.push_back(ap);
    }
    return birey;
}

double uzaklik(int x1, int y1, int x2, int y2) {
    return sqrt((x1-x2)*(x1-x2) + (y1-y2)*(y1-y2));
}

double uygunluk(vector<AP>& birey) {
    double kapsanan = 0.0, toplam_uzaklik = 0.0;
    vector<double> kapasite_kullanim(AP_SAYISI, 0.0);
    int kanal_cezasi = 0, kapsanamayan = 0;

    for (size_t i = 0; i < kullanicilar.size(); i++) {
        vector<tuple<int, double>> uygun_apler;
        for (size_t j = 0; j < birey.size(); j++) {
            double mesafe = uzaklik(kullanicilar[i].x, kullanicilar[i].y, birey[j].x, birey[j].y);
            if (mesafe <= 30) {
                uygun_apler.emplace_back(j, mesafe);
            }
        }
        if (!uygun_apler.empty()) {
            sort(uygun_apler.begin(), uygun_apler.end(), [](auto& a, auto& b) { return get<1>(a) < get<1>(b); });
            int secilen = get<0>(uygun_apler[0]);
            kapasite_kullanim[secilen] += kullanicilar[i].talep;  // taştığında hangisi?
            kapsanan += kullanicilar[i].talep;
            toplam_uzaklik += get<1>(uygun_apler[0]);
        } else kapsanamayan++;
    }

    for (int i = 0; i < AP_SAYISI; i++) {
        for (int j = i+1; j < AP_SAYISI; j++) {
            double d = uzaklik(birey[i].x, birey[i].y, birey[j].x, birey[j].y);
            if (birey[i].kanal == birey[j].kanal && d < 50) kanal_cezasi++;
        }
    }

    return kapsanan - 0.1*toplam_uzaklik - 5*kapsanamayan - 2*kanal_cezasi;
}

vector<AP> crossover(const vector<AP>& a, const vector<AP>& b) {
    int nokta = randint(1, AP_SAYISI);
    vector<AP> yc;
    for (int i = 0; i < nokta; i++) yc.push_back(a[i]);
    for (int i = nokta; i < AP_SAYISI; i++) yc.push_back(b[i]);
    return yc;
}

vector<AP> mutasyon(vector<AP> birey) {
    for (auto& ap : birey) {
        if (rand01(gen) < 0.05) ap.kanal = randint(1, 14);
    }
    return birey;
}

// ------------------------------------------------------
// REST Sunucusu (cpp-httplib) ve JSON Oluşturma
// ------------------------------------------------------

void baslatRESTServer() {
    httplib::Server svr;

    svr.Get("/best", [&](const httplib::Request&, httplib::Response& res) {
        // 🔥 JSON hatası ve potansiyel buffer overflow
        string json = "{ \"en_iyi_skor\": " + to_string(en_iyi_skor) + ", \"aps\": [";
        for (size_t i = 0; i < en_iyi_birey.size(); i++) {
            json += "{ \"id\": " + to_string(i)
                  + ", \"x\": " + to_string(en_iyi_birey[i].x)
                  + ", \"y\": " + to_string(en_iyi_birey[i].y)
                  + ", \"kanal\": " + to_string(en_iyi_birey[i].kanal)
                  + ", \"label\": \"" + en_iyi_birey[i].label + "\" },";
        }
        if (!en_iyi_birey.empty()) {
            json.back() = ']';
        } else {
            json += "]";
        }
        json += " }";
        res.set_content(json, "application/json");
    });

    svr.listen("0.0.0.0", 8080); // Hata kontrolü yok
}

// ------------------------------------------------------
// Thread Fonksiyonları
// ------------------------------------------------------

void* fitnessThread(void* arg) {
    while (!dur) {
        double toplam = 0;
        for (auto& k : kullanicilar) toplam += k.talep;  // k talep uninitialized olabilir
        globalOrtalamaFitness = kullanicilar.empty() ? 0 : toplam / kullanicilar.size();
        sleep(1);
    }
    return nullptr;
}

// ------------------------------------------------------
// Terminal Menüsü (ncurses)
// ------------------------------------------------------

void terminalMenu() {
    initscr(); cbreak(); noecho();
    int choice = 0;
    while (choice != '3') {
        clear();
        mvprintw(1, 1, "1) Genetik Algoritma Baslat");
        mvprintw(2, 1, "2) Log dosyasini goster");
        mvprintw(3, 1, "3) Cikis");
        refresh();
        choice = getch();
        switch (choice) {
            case '1': mvprintw(5,1,"GA calisiyor..."); refresh(); sleep(1); break;
            case '2': mvprintw(5,1,"Log dosyasi: optimal.txt"); refresh(); sleep(1); break;
            default: break;
        }
    }
    endwin();
}

// ------------------------------------------------------
// Görsel Oluşturma (OpenCV Stub)
// ------------------------------------------------------

void gorselOlustur(const vector<AP>& ekip) {
#ifdef USE_OPENCV
    cv::Mat img(100, 100, CV_8UC3, cv::Scalar(255,255,255));
    for (auto& k : ekip) {
        if (k.x >= 0 && k.x < 100 && k.y >= 0 && k.y < 100) {
            cv::circle(img, cv::Point(k.x, k.y), 3, cv::Scalar(0,0,255), -1);
        }
    }
    cv::imwrite("kullanici_haritasi.png", img);  // İzin hatası riski
#else
    cout << "[OPENCV KAPALI] Kullanici haritasi olusturulamadi.\n";
#endif
}

// ------------------------------------------------------
// Ana Fonksiyon
// ------------------------------------------------------

int main(int argc, char* argv[]) {
    srand(time(0));

    // Command injection zafiyeti
    char komut[256];
    snprintf(komut, sizeof(komut), "echo %s >> log.txt", configDosya.c_str());
    system(komut);

    // Kullanıcıları konfig dosyasından oku
    konfigDosyasiniOku(configDosya.c_str());

    // Rastgele kullanıcı ekle (eğer config boşsa)
    if (kullanicilar.empty()) {
        for (int i = 0; i < 5; i++) {
            AP k; k.x = randint(0, 100); k.y = randint(0, 100);
            k.kanal = randint(1, 14); k.talep = rand01(gen) * 5;
            snprintf(k.label, sizeof(k.label), "K%d", i);
            kullanicilar.push_back(k);
        }
    }

    // Veritabanını aç
    veritabaniAc("wifi_ap.db");

    // Zafiyet testleri
    test_null_pointer();
    belirsiz_kullan();

    // Genetik Algoritma: Popülasyon oluştur ve çalıştır
    AP_SAYISI = 5;
    vector<vector<AP>> populasyon;
    for (int i = 0; i < AP_SAYISI; i++) populasyon.push_back(rastgele_birey());

    for (int epoch = 0; epoch < 100; epoch++) {
        vector<pair<double, vector<AP>>> skorlu;
        for (auto& birey : populasyon) {
            skorlu.push_back({uygunluk(const_cast<vector<AP>&>(birey)), birey});
        }
        sort(skorlu.begin(), skorlu.end(), [](auto& a, auto& b) { return a.first > b.first; });
        if (skorlu[0].first > en_iyi_skor) {
            en_iyi_skor = skorlu[0].first;
            en_iyi_birey = skorlu[0].second;
        }
        vector<vector<AP>> yeniPop;
        for (int i = 0; i < 2; i++) yeniPop.push_back(skorlu[i].second);
        while ((int)yeniPop.size() < AP_SAYISI) {
            int a = randint(0,2), b = randint(0,2);
            auto cocuk = crossover(yeniPop[a], yeniPop[b]);
            cocuk = mutasyon(cocuk);
            yeniPop.push_back(cocuk);
        }
        populasyon = yeniPop;
    }

    // Sonuçları kaydet
    kaydetOptimalYerlesim(en_iyi_birey);
    veritabaniyeYaz(en_iyi_birey);

    // Thread zafiyetleri: eş zamanlı log yazma ve race
    pthread_t t1, t2;
    pthread_create(&t1, nullptr, fitnessThread, nullptr);
    pthread_create(&t2, nullptr, raceConditionThread, nullptr);
    sleep(5);
    dur = true;
    pthread_join(t1, nullptr);
    pthread_join(t2, nullptr);

    // REST sunucusu
    baslatRESTServer();

    // Terminal menüsü
    terminalMenu();

    // Görsel oluştur
    gorselOlustur(kullanicilar);

    // Veritabanını kapat
    veritabaniKapat();

    cout << "\nFinal skor: " << en_iyi_skor << endl;
    return 0;
}
