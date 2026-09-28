// GenryBL V1 - weather particles. One description of every weather (layers of soft particles
// at three depths) drives both the game (SnowBlossom layers + the particle PNGs the builder
// writes into the mod) and the editor preview, so «погода снег» looks the same in both.
#pragma once
#include <QImage>
#include <QString>
#include <QStringList>
#include <QVector>

namespace gb {

struct WeatherLayer {
    QString png;                  // particle picture (genry_fx/<png>.png)
    double zoom = 1.0, alpha = 1.0;
    int count = 20;               // at level 2 («обычно»)
    int xs0 = 0, xs1 = 0;         // SnowBlossom xspeed range, px/s
    int ys0 = 50, ys1 = 100;      // yspeed; negative = floats up
    QString anim;                 // "", "spin", "spinfast", "twinkle"
    double rotate = 0;            // fixed tilt (rain)
};

QStringList weatherParticleNames();                         // snow rain leaf leaf2 heart spark dust
QImage weatherParticle(const QString& name);               // procedurally drawn, deterministic
QVector<WeatherLayer> weatherLayers(const QString& key);   // snow/rain/leaf/heart/spark/dust
QString weatherTint(const QString& key, int level);        // "#0b162630" for rain, "" otherwise
// «слабо»/«лёгкий» -> 1, «сильно»/«метель» -> 3, anything else 0 (not a level word)
int weatherLevelWord(const QString& word);
double weatherLevelFactor(int level);                      // count multiplier: 0.5 / 1 / 1.8

} // namespace gb
