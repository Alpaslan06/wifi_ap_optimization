#include <iostream>
#include <vector>
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
#include <unistd.h>     // sleep
#include <ncurses.h>    // ncurses
#ifdef USE_OPENCV
#include <opencv2/opencv.hpp>  // OpenCV
#endif
#include "httplib.h"    // httplib.h (cpp-httplib)

using namespace std;

// ------------------------------------------------------
// Temel Yapılar
// ------------------------------------------------------

struct Kullanici {
    int x, y;
    double talep;
};

struct AP {
    int x, y;
    int kanal;
    char* label;      // 🔥 Bellek sızıntısı için eklendi ama delete edilmeyecek
};

const int ALAN_BOYUTU = 100;
const int N_POP = 50;
const int N_EPOCH = 600;
int KAPSAMA;
int AP_SAYISI = 0, KULLANICI_SAYISI = 0;
vector<Kullanici> kullanicilar;
vector<AP> en_iyi_birey;
double en_iyi_skor = -1e9;

// Sabit sayı sihirli olarak verildi (magic number)
int SIHIRLI_ESIK = 1234;             // 🔥 Magic constant
const int MAX_LABEL_LEN = 16;        // 🔥 Buffer overflow potansiyeli

// SQLite
sqlite3* db = nullptr;

// Thread kontrolü
double globalOrtalamaFitness = 0.0;
bool dur = false;

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

// 🔥 Null pointer dereference için test fonksiyonu
void test_null_pointer() {
    int* ptr = nullptr;
    if (randint(0, 20) == 7) {
        *ptr = 10;  // NULL pointer dereference
    }
}

// 🔥 Uninitialized variable hatası
int belirsiz_kullan() {
    int x;               // 🔥 Başlatılmadı
    return x + 1;        // Uninitialized variable kullanımı
}

// ------------------------------------------------------
// Dosya I/O: Konfig Okuma & Optimal Yerleşim Kaydetme
// ------------------------------------------------------

// Kullanıcıları ekrandan okuduktan sonra bir metin dosyasından konfigürasyon
// okumayı sağlayacak işlev. Burada buffer overflow ve strtok hatası var.
void konfigDosyasiniOku(const char* dosyaAdi) {
    FILE* fp = fopen(dosyaAdi, "r");   // 🔥 fopen NULL kontrolü yok
    if (!fp) {
        // Eğer dosya açılmazsa sessizce çık (hoca yok demesin)
        return;
    }
    char satir[64];                    // 🔥 Eğer satır > 63 char olursa overflow
    while (fgets(satir, 64, fp) != nullptr) {
        // Örnek satır: "10,20,2.5"
        char* token = strtok(satir, ",");
        if (!token) continue;
        int x = atoi(token);
        
        token = strtok(nullptr, ",");
        if (!token) continue;
        int y = atoi(token);

        token = strtok(nullptr, "\n");
        if (!token) continue;
        double t = atof(token);

        // 🔥 strcpy ile AP label kopyalarken overflow riski
        char labelKaynak[32];
        snprintf(labelKaynak, sizeof(labelKaynak), "%d_%d", x, y);
        char* label = (char*)malloc(MAX_LABEL_LEN);
        if (label) {
            strcpy(label, labelKaynak); // 🔥 Eğer labelKaynak uzun ise overflow
        }

        kullanicilar.push_back({x, y, t});
    }
    // 🔥 fclose(fp) missing guard
    fclose(fp);
}

// Optimal AP yerleşimlerini bir dosyaya yazan fonksiyon.
// Burada fopen/fwrite hata kontrolü eksik, ayrıca canlı loglama ekledik.
void kaydetOptimalYerlesim(const vector<AP>& optimal) {
    FILE* fout = fopen("optimal.txt", "w");  // 🔥 fopen dönüş kontrolü yok
    if (!fout) return;
    for (size_t i = 0; i < optimal.size(); i++) {
        // 🔥 Off-by-one riski yok ama fprintf hata kontrolü yok
        fprintf(fout, "AP #%zu: x=%d y=%d kanal=%d label=%s\n", 
                i, optimal[i].x, optimal[i].y, optimal[i].kanal, optimal[i].label);
    }
    // 🔥 fclose hatası, hata kontrolü yok
    fclose(fout);
}

// ------------------------------------------------------
// SQLite Veritabanı İşlemleri
// ------------------------------------------------------

// Veritabanını açan fonksiyon
void veritabaniAc(const char* dbAdi) {
    int rc = sqlite3_open(dbAdi, &db);
    // 🔥 Hata kontrolü yapılmıyor. Eğer `db` pointer NULL dönerse NULL dereference.
    if (rc != SQLITE_OK) {
        db = nullptr;
    }
}

// Veritabanını kapatan fonksiyon
void veritabaniKapat() {
    if (db) sqlite3_close(db);
}

// AP yerleşim sonuçlarını tabloya ekleyen basit fonksiyon:
void veritabaniyeYaz(const vector<AP>& optimal) {
    if (!db) return;
    // Tablo yoksa oluştur
    const char* createSQL = 
        "CREATE TABLE IF NOT EXISTS yerlesim ("
        "ap_id INTEGER, x INTEGER, y INTEGER, kanal INTEGER, label TEXT);";
    char* hataMesaji = nullptr;
    sqlite3_exec(db, createSQL, nullptr, 0, &hataMesaji);
    if (hataMesaji) sqlite3_free(hataMesaji);

    for (size_t i = 0; i < optimal.size(); i++) {
        char sql[256];
        // 🔥 snprintf boyut kontrolü eksiktir, SQL stringi uzun olursa taşma
        snprintf(sql, sizeof(sql),
                 "INSERT INTO yerlesim (ap_id, x, y, kanal, label) "
                 "VALUES (%zu, %d, %d, %d, '%s');",
                 i, optimal[i].x, optimal[i].y, optimal[i].kanal, optimal[i].label);
        hataMesaji = nullptr;
        int req = sqlite3_exec(db, sql, nullptr, 0, &hataMesaji);
        if (req != SQLITE_OK && hataMesaji) {
            // 🔥 sqlite3_free hata kontrolü yok
            sqlite3_free(hataMesaji);
        }
    }
}

// ------------------------------------------------------
// Ağ Zamanı Simülasyonu (Integer Overflow Potansiyeli)
// ------------------------------------------------------

// Basit bir network paketi simülasyonu stub'u.
// Burada “paketBoyutu * 1024” işlemi integer overflow verebilir.
int agZamaniSimulasyonu(int paketBoyutu) {
    int hesap = paketBoyutu * 1024;    // 🔥 Eğer paketBoyutu büyükse overflow
    // 20ms sabit köşe süresi (magic number)
    if (hesap > 100000) {
        return 5;   // örnek gecikme
    }
    return hesap / 100; // basitçe geriye değer dön
}

// ------------------------------------------------------
// AP Dizisi ve Rastgele Birey Oluşturma
// ------------------------------------------------------

// AP label’larını oluştur, ama hem new hem malloc mix’leniyor.
vector<AP> rastgele_birey() {
    vector<AP> birey;
    for (int i = 0; i < AP_SAYISI; i++) {
        // 🔥 new ve malloc karışımı
        AP ap;
        ap.x = randint(0, ALAN_BOYUTU);
        ap.y = randint(0, ALAN_BOYUTU);
        ap.kanal = randint(1, 14);
        
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "AP_%d_%d", ap.x, ap.y);
        ap.label = (char*)malloc(MAX_LABEL_LEN); // 🔥 Memory leak (free yapılmadı)
        if (ap.label) {
            strcpy(ap.label, buffer); // 🔥 Overflow riski
        }
        
        birey.push_back(ap);
    }
    return birey;
}

// ------------------------------------------------------
// Temel Matematik: Mesafe ve Fitness
// ------------------------------------------------------

double uzaklik(int x1, int y1, int x2, int y2) {
    return sqrt((x1 - x2)*(x1 - x2) + (y1 - y2)*(y1 - y2));
}

double uygunluk(vector<AP>& birey) {
    double kapsanan = 0.0, toplam_uzaklik = 0.0;
    vector<double> kapasite_kullanim(AP_SAYISI, 0.0);
    int kanal_cezasi = 0, kapsanamayan = 0, fiziksel_ceza = 0, kapsama_cezasi = 0;

    // 🔥 Kullanılmayan bir değer
    int bos_deger = 42;
    bos_deger = 7;

    for (int i = 0; i < (int)kullanicilar.size(); i++) {
        vector<tuple<int, double, int>> uygun_apler;
        for (int j = 0; j < (int)birey.size(); j++) {
            double mesafe = uzaklik(kullanicilar[i].x, kullanicilar[i].y, birey[j].x, birey[j].y);
            if (mesafe <= KAPSAMA) {
                uygun_apler.emplace_back(j, mesafe, birey[j].kanal);
            }
        }
        if (!uygun_apler.empty()) {
            sort(uygun_apler.begin(), uygun_apler.end(), [&](auto& a, auto& b) {
                return tie(kapasite_kullanim[get<0>(a)], get<1>(a)) 
                     < tie(kapasite_kullanim[get<0>(b)], get<1>(b));
            });
            int secilen = get<0>(uygun_apler[0]);
            kapasite_kullanim[secilen] += kullanicilar[i].talep;
            kapsanan += kullanicilar[i].talep;
            toplam_uzaklik += get<1>(uygun_apler[0]);
        } else kapsanamayan++;
    }

    for (int i = 0; i < AP_SAYISI; i++) {
        for (int j = i + 1; j < AP_SAYISI; j++) {
            double d = uzaklik(birey[i].x, birey[i].y, birey[j].x, birey[j].y);
            if (birey[i].kanal == birey[j].kanal && d < KAPSAMA * 1.5) kanal_cezasi++;
            if (d < 20) fiziksel_ceza++;
            if (d < 2 * KAPSAMA) kapsama_cezasi++;
        }
    }

    double kapasite_cezasi = 0.0;
    for (auto k : kapasite_kullanim) if (k > 8.0) kapasite_cezasi += (k - 8.0);

    // 🔥 Magic number kullanımı
    if (kapsanan > SIHIRLI_ESIK) {
        cout << "Sihirli eşik aşıldı!" << endl;
    }

    // Sonuç döndür
    return kapsanan
           - 0.05 * toplam_uzaklik
           - 4 * kanal_cezasi
           - 1.5 * kapasite_cezasi
           - 5 * kapsanamayan
           - 2 * fiziksel_ceza
           - 3 * kapsama_cezasi;
}

// ------------------------------------------------------
// Genetik Algoritma: Crossover & Mutasyon
// ------------------------------------------------------

vector<AP> crossover(const vector<AP>& anne, const vector<AP>& baba) {
    int nokta = randint(1, AP_SAYISI);
    vector<AP> yeni;
    yeni.insert(yeni.end(), anne.begin(), anne.begin() + nokta);
    yeni.insert(yeni.end(), baba.begin() + nokta, baba.end());
    return yeni;
}

vector<AP> mutasyon(vector<AP> birey, double oran = 0.02) {
    for (int i = 0; i < (int)birey.size(); i++) {
        if (rand01(gen) < oran) birey[i].x = randint(0, ALAN_BOYUTU), birey[i].y = randint(0, ALAN_BOYUTU);
        if (rand01(gen) < oran) birey[i].kanal = randint(1, 14);
    }
    return birey;
}

// ------------------------------------------------------
// REST Sunucusu (cpp-httplib) ve JSON Oluşturma
// ------------------------------------------------------

void baslatRESTServer() {
    httplib::Server svr;

    svr.Get("/best", [&](const httplib::Request&, httplib::Response& res) {
        // 🔥 JSON üretimi için elle string birleştirme, yanlış tırnak veya null kontrol eksik
        string json = "{ \"en_iyi_skor\": " + to_string(en_iyi_skor) + ", \"aps\": [";
        for (size_t i = 0; i < en_iyi_birey.size(); i++) {
            // 🔥 char* label taşması veya null olabilir
            json += "{ \"id\": " + to_string(i)
                  + ", \"x\": " + to_string(en_iyi_birey[i].x)
                  + ", \"y\": " + to_string(en_iyi_birey[i].y)
                  + ", \"kanal\": " + to_string(en_iyi_birey[i].kanal)
                  + ", \"label\": \"" + (en_iyi_birey[i].label ? en_iyi_birey[i].label : "") + "\" },";
        }
        if (!en_iyi_birey.empty()) {
            json.back() = ']'; // 🔥 Varsa hiç AP yoksa back crash’ı
        } else {
            json += "]";       // Liste boşsa bile kapanışı yap
        }
        json += " }";
        res.set_content(json, "application/json");
    });

    svr.Get("/status", [&](const httplib::Request&, httplib::Response& res) {
        double ortalama = 0;
        if (!kullanicilar.empty()) {
            double toplam = 0;
            for (auto& k : kullanicilar) toplam += k.talep;
            ortalama = toplam / kullanicilar.size();
        }
        res.set_content("Ortalama talep: " + to_string(ortalama), "text/plain");
    });

    svr.listen("0.0.0.0", 8080); // 🔥 Eğer 8080 meşgulse hata kontrolü yok
}

// ------------------------------------------------------
// Thread Fonksiyonları
// ------------------------------------------------------

// Fitness değerini sürekli güncelleyen thread
void* fitnessThread(void* arg) {
    while (!dur) {
        double toplam = 0;
        for (auto& k : kullanicilar) toplam += k.talep;
        globalOrtalamaFitness = kullanicilar.empty() ? 0 : toplam / kullanicilar.size();
        sleep(1);
    }
    return nullptr;
}

// REST sunucusunu çalıştıran thread
void* restThread(void* arg) {
    baslatRESTServer();  // HTTP sunucusunu çalıştırıyor
    return nullptr;
}

// ------------------------------------------------------
// Terminal Menüsü (ncurses)
// ------------------------------------------------------

void terminalMenu() {
    initscr();
    cbreak();
    noecho();
    int choice = 0;
    while (choice != '3') {
        clear();
        mvprintw(1, 1, "1) Genetik Algoritma Baslat");
        mvprintw(2, 1, "2) Log dosyasini goster");
        mvprintw(3, 1, "3) Cikis");
        refresh();
        choice = getch();

        switch (choice) {
            case '1':
                mvprintw(5, 1, "GA calisiyor...");
                refresh();
                sleep(1);
                break;
            case '2':
                mvprintw(5, 1, "Log dosyasi: optimal.txt");
                refresh();
                sleep(1);
                break;
            default:
                break;
        }
    }
    endwin();
}

// ------------------------------------------------------
// Görsel Oluşturma (OpenCV Stub)
// ------------------------------------------------------

void gorselOlustur(const vector<Kullanici>& kullanicilar) {
#ifdef USE_OPENCV
    cv::Mat img(ALAN_BOYUTU, ALAN_BOYUTU, CV_8UC3, cv::Scalar(255,255,255));
    for (auto& k : kullanicilar) {
        // 🔥 Koordinat dışına çıkarsa çizimde hata
        if (k.x >= 0 && k.x < ALAN_BOYUTU && k.y >= 0 && k.y < ALAN_BOYUTU) {
            cv::circle(img, cv::Point(k.x, k.y), 3, cv::Scalar(0,0,255), -1);
        }
    }
    cv::imwrite("kullanici_haritasi.png", img); // 🔥 Disk izni sorunu olabilir
#else
    cout << "[OPENCV KAPALI] Kullanici haritasi olusturulamadi.\n"; 
#endif
}

// ------------------------------------------------------
// Ana Fonksiyon
// ------------------------------------------------------

int main(int argc, char* argv[]) {
    srand(time(0));

    // --------------------------------------------------
    // 1) Komut Satırı Argümanları ile AP ve Kullanıcı Sayısı, Dosya İsimleri
    // --------------------------------------------------

    string configDosya = "config.txt";
    string logDosya    = "optimal.txt";
    int opt;
    static struct option longOptions[] = {
        {"ap-sayisi",        required_argument, 0, 'a'},
        {"kullanici-sayisi", required_argument, 0, 'u'},
        {"config",           required_argument, 0, 'c'},
        {"log",              required_argument, 0, 'l'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "a:u:c:l:", longOptions, nullptr)) != -1) {
        switch (opt) {
            case 'a': AP_SAYISI = atoi(optarg); break;   // 🔥 atoi kontrol yok
            case 'u': KULLANICI_SAYISI = atoi(optarg); break;
            case 'c': configDosya = optarg; break;
            case 'l': logDosya    = optarg; break;
            default: break;
        }
    }
    if (AP_SAYISI <= 0) {
        cout << "Kaç adet Access Point yerleştirilsin? "; cin >> AP_SAYISI;
    }
    if (KULLANICI_SAYISI <= 0) {
        cout << "Kaç adet kullanıcı yerleştirilsin? "; cin >> KULLANICI_SAYISI;
    }
    KAPSAMA = max(20, int(30 * sqrt(double(KULLANICI_SAYISI) / (5 * AP_SAYISI))));

    // --------------------------------------------------
    // 2) Zafiyet Test Fonksiyonları
    // --------------------------------------------------
    test_null_pointer();      // NULL pointer tetikler (arada crash olursa Coverity yakalar)
    belirsiz_kullan();        // Uninitialized variable

    // --------------------------------------------------
    // 3) Dosya I/O: Konfig Okuma (configDosya) ve Kullanıcıları Eklemek
    // --------------------------------------------------
    konfigDosyasiniOku(configDosya.c_str());

    // Eğer config dosyasında kullanıcı yoksa rastgele oluştur
    for (int i = 0; i < KULLANICI_SAYISI; i++) {
        int x = randint(0, ALAN_BOYUTU), y = randint(0, ALAN_BOYUTU);
        double talep_degeri[] = {0.5, 1.0, 2.0, 3.0};
        double p[] = {0.3, 0.3, 0.2, 0.2};
        double r = rand01(gen), toplam = 0.0;
        for (int j = 0; j < 4; j++) {
            toplam += p[j];
            if (r <= toplam) {
                kullanicilar.push_back({x, y, talep_degeri[j]});
                break;
            }
        }
    }

    // --------------------------------------------------
    // 4) SQLite Veritabanı Ayarları
    // --------------------------------------------------
    veritabaniAc("wifi_ap.db");
    // Tabloyu ve veritabanını kullanacağız sonrasında

    // --------------------------------------------------
    // 5) Thread’leri Başlat
    // --------------------------------------------------
    pthread_t t1, t2;
    // 🔥 pthread_create dönüş kontrolü yok
    pthread_create(&t1, nullptr, fitnessThread, nullptr);
    pthread_create(&t2, nullptr, restThread, nullptr);

    // --------------------------------------------------
    // 6) Genetik Algoritma: AP Popülasyonunu Oluştur
    // --------------------------------------------------
    vector<vector<AP>> populasyon;
    for (int i = 0; i < N_POP; i++) populasyon.push_back(rastgele_birey());

    // --------------------------------------------------
    // 7) GA Döngüsü
    // --------------------------------------------------
    for (int epoch = 0; epoch < N_EPOCH; epoch++) {
        vector<pair<double, vector<AP>>> skorlu;
        for (auto& birey : populasyon)
            skorlu.push_back({uygunluk(birey), birey});

        sort(skorlu.begin(), skorlu.end(), [](auto& a, auto& b) { return a.first > b.first; });
        populasyon.clear();
        for (int i = 0; i < 10; i++) populasyon.push_back(skorlu[i].second);

        if (skorlu[0].first > en_iyi_skor) {
            en_iyi_skor = skorlu[0].first;
            en_iyi_birey = skorlu[0].second;
        }

        while (populasyon.size() < N_POP - 1) {
            int a = randint(0, 10), b = randint(0, 10);
            auto cocuk = crossover(populasyon[a], populasyon[b]);
            cocuk = mutasyon(cocuk);
            populasyon.push_back(cocuk);
        }
        populasyon.push_back(en_iyi_birey);

        if (epoch % 50 == 0)
            cout << "Epoch " << epoch << " - En iyi skor: " << en_iyi_skor << endl;
    }

    // --------------------------------------------------
    // 8) Çıktıları Kaydet
    // --------------------------------------------------
    kaydetOptimalYerlesim(en_iyi_birey);            // Dosyaya yaz
    veritabaniyeYaz(en_iyi_birey);                  // Veritabanına yaz

    // --------------------------------------------------
    // 9) Ağ Zamanı Simülasyonu
    // --------------------------------------------------
    int gecikme = agZamaniSimulasyonu(50000);
    cout << "Ağ gecikmesi aproximated (ms): " << gecikme << endl;

    // --------------------------------------------------
    // 10) Görsel Oluşturma (OpenCV)
    // --------------------------------------------------
    gorselOlustur(kullanicilar);

    // --------------------------------------------------
    // 11) Terminal Menüsü (ncurses)
    // --------------------------------------------------
    terminalMenu();

    // --------------------------------------------------
    // 12) Thread’leri Durdur
    // --------------------------------------------------
    dur = true;
    pthread_join(t1, nullptr);
    pthread_join(t2, nullptr);

    // --------------------------------------------------
    // 13) Veritabanını Kapama
    // --------------------------------------------------
    veritabaniKapat();

    cout << "\nFinal skor: " << en_iyi_skor << endl;
    return 0;
}
