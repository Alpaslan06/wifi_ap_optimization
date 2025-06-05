// Wi-Fi Access Point Yerleşimi - Genetik Algoritma (C++ Versiyon)
// Kodun özgünlüğü korunarak Python versiyonundan esinlenerek C++'a çevrilmiştir.
// Kullanıcı girdisiyle dinamik AP yerleşimi, genetik algoritma, uygunluk fonksiyonu ve görselleştirme (matplotlib-cpp önerilir)

#include <iostream>
#include <vector>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <random>

using namespace std;

struct Kullanici {
    int x, y;
    double talep;
};

struct AP {
    int x, y;
    int kanal;
};

const int ALAN_BOYUTU = 100;
const int N_POP = 50;
const int N_EPOCH = 600;
int KAPSAMA;
int AP_SAYISI, KULLANICI_SAYISI;
vector<Kullanici> kullanicilar;

// Rastgele sayı üretimi için sabitleyici
random_device rd;
mt19937 gen(rd());
uniform_real_distribution<> rand01(0.0, 1.0);

int randint(int min, int max) {
    uniform_int_distribution<> dis(min, max - 1);
    return dis(gen);
}

double uzaklik(int x1, int y1, int x2, int y2) {
    return sqrt((x1 - x2)*(x1 - x2) + (y1 - y2)*(y1 - y2));
}

vector<AP> rastgele_birey() {
    vector<AP> birey;
    for (int i = 0; i < AP_SAYISI; i++) {
        birey.push_back({randint(0, ALAN_BOYUTU), randint(0, ALAN_BOYUTU), randint(1, 14)});
    }
    return birey;
}

double uygunluk(vector<AP>& birey) {
    double kapsanan = 0.0, toplam_uzaklik = 0.0;
    vector<double> kapasite_kullanim(AP_SAYISI, 0.0);
    int kanal_cezasi = 0, kapsanamayan = 0, fiziksel_ceza = 0, kapsama_cezasi = 0;

    for (int i = 0; i < kullanicilar.size(); i++) {
        vector<tuple<int, double, int>> uygun_apler;
        for (int j = 0; j < birey.size(); j++) {
            double mesafe = uzaklik(kullanicilar[i].x, kullanicilar[i].y, birey[j].x, birey[j].y);
            if (mesafe <= KAPSAMA) {
                uygun_apler.emplace_back(j, mesafe, birey[j].kanal);
            }
        }
        if (!uygun_apler.empty()) {
            sort(uygun_apler.begin(), uygun_apler.end(), [&](auto& a, auto& b) {
                return tie(kapasite_kullanim[get<0>(a)], get<1>(a)) < tie(kapasite_kullanim[get<0>(b)], get<1>(b));
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

    double skor = kapsanan - 0.05 * toplam_uzaklik - 4 * kanal_cezasi - 1.5 * kapasite_cezasi
                  - 5 * kapsanamayan - 2 * fiziksel_ceza - 3 * kapsama_cezasi;
    return skor;
}

vector<AP> crossover(const vector<AP>& anne, const vector<AP>& baba) {
    int nokta = randint(1, AP_SAYISI);
    vector<AP> yeni;
    yeni.insert(yeni.end(), anne.begin(), anne.begin() + nokta);
    yeni.insert(yeni.end(), baba.begin() + nokta, baba.end());
    return yeni;
}

vector<AP> mutasyon(vector<AP> birey, double oran = 0.02) {
    for (int i = 0; i < birey.size(); i++) {
        if (rand01(gen) < oran) birey[i].x = randint(0, ALAN_BOYUTU), birey[i].y = randint(0, ALAN_BOYUTU);
        if (rand01(gen) < oran) birey[i].kanal = randint(1, 14);
    }
    return birey;
}

int main() {
    srand(time(0));
    cout << "Kaç adet Access Point yerleştirilsin? "; cin >> AP_SAYISI;
    cout << "Kaç adet kullanıcı yerleştirilsin? "; cin >> KULLANICI_SAYISI;
    KAPSAMA = max(20, int(30 * sqrt(double(KULLANICI_SAYISI) / (5 * AP_SAYISI))));

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

    vector<vector<AP>> populasyon;
    for (int i = 0; i < N_POP; i++) populasyon.push_back(rastgele_birey());

    double en_iyi_skor = -1e9;
    vector<AP> en_iyi_birey;

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

    cout << "\n Final skor: " << en_iyi_skor << endl;
    return 0;
}
