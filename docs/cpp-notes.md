# C++ notları

godot-dis'te ilk kez kullanılan C++ ve GDExtension kavramları, kısa açıklamalarla. Bir kavram koda ilk girdiğinde
buraya eklenir. Örnekler bu repodaki dosyalardan.

## Derleme zinciri (Faz 0)

**Derleme ne yapar?** `.cpp` dosyaları tek tek *derlenir* (compile) ve her biri bir nesne dosyası (`.obj`) olur.
Sonra *bağlayıcı* (linker) bunları tek bir kütüphanede birleştirir: Windows'ta `.dll`, Linux'ta `.so`,
macOS'ta `.dylib`. Godot bu kütüphaneyi çalışma anında yükler.

**Araçlar:**
- **MSVC (`cl.exe`)**: Visual Studio'nun C++ derleyicisi. Windows'ta Godot ile en uyumlu seçenek.
- **SCons**: Python ile yazılmış derleme aracı. Godot ve godot-cpp bunu kullanır. `SConstruct` dosyası bir
  Python betiğidir: hangi dosyaların, hangi ayarlarla derleneceğini söyler. `scons -j16` 16 dosyayı paralel derler.
- **godot-cpp**: Godot'un C API'sini (GDExtension) C++ sınıflarıyla saran kütüphane. `Node`, `RefCounted`,
  `PackedByteArray` gibi Godot tipleri buradan gelir. Bir kez statik kütüphane (`.lib`) olarak derlenir, sonra
  bizim DLL'imize gömülür. İlk derleme ~1 dakika sürer, sonrakiler yalnızca değişen dosyaları derler.
- **git submodule**: Başka bir git deposunu belirli bir commit'e sabitlenmiş halde alt klasör olarak eklemek.
  `thirdparty/godot-cpp` ve `thirdparty/open-dis-cpp` böyle. Klonlarken: `git clone --recursive`.

**`api_version=4.3`**: godot-cpp'ye "Godot 4.3'ün API'sini hedefle" der. Bu sayede aynı DLL Godot 4.3, 4.4, ...,
4.7'de çalışır (eski API'yi hedefleyen eklenti yeni Godot'ta çalışır, tersi çalışmaz).

**`template_debug` / `template_release`**: Godot editörü ve debug dışa aktarımları `template_debug` DLL'ini,
release dışa aktarımları `template_release` DLL'ini yükler. Hangisinin hangi dosya olduğu
`addons/dis/dis.gdextension` içinde yazar.

## Dosya yapısı

**Başlık (`.h`) ve kaynak (`.cpp`)**: C++'ta bir sınıfın *bildirimi* (hangi fonksiyonları var) başlık dosyasında,
*tanımı* (fonksiyonların gövdesi) kaynak dosyasında olur. Başka dosyalar yalnızca başlığı `#include` eder.
Örnek: `src/dis_probe.h` ve `src/dis_probe.cpp`.

**`#include <...>` ve `#include "..."`**: Derleyici dosyanın içeriğini olduğu gibi oraya kopyalar. `<...>` arama
yollarında (`CPPPATH`) arar, `"..."` önce aynı klasörde. open-dis-cpp başlıklarını `<dis7/EntityStatePdu.h>`
diye ister; bu yüzden `SConstruct`'ta `thirdparty/open-dis-cpp/src/` arama yoluna eklendi.

**`#pragma once`**: Aynı başlık bir derleme biriminde iki kez eklenirse ikinciyi yok sayar.

**Makro (`#define`)**: Derlemeden önce yapılan metin değiştirme. open-dis-cpp sınıflarının başında
`OPENDIS7_EXPORT` makrosu var (DLL'den sembol dışa aktarmak için). Biz open-dis-cpp'yi kendi DLL'imizin *içine*
derlediğimiz için `src/compat/dis7/opendis7_export.h` bu makroyu boş tanımlıyor.

## Dil kavramları

**Namespace (`DIS::EntityStatePdu`, `godot::PackedByteArray`)**: İsim çakışmasını önleyen ad alanı.
`using namespace godot;` o dosyada `godot::` önekini yazmamayı sağlar (başlık dosyalarında yapılmaz).

**Sınıf ve kalıtım**: `class DisProbe : public godot::RefCounted` → `DisProbe`, `RefCounted`'ın her şeyini miras alır.
GDScript'teki `extends RefCounted` ile aynı fikir.

**`static` fonksiyon**: Nesne oluşturmadan çağrılır: `DisProbe.encode_test_entity_state()`.

**Referans (`const EntityID &pX`)**: Nesnenin kopyasını değil, kendisini geçirir (kopyalamadan, hızlı).
`const` "bu fonksiyon onu değiştirmeyecek" sözüdür.

**`static_cast<T>(x)`**: Açık, derleme zamanında kontrol edilen tip dönüşümü. Örnek: `size_t` (64-bit, işaretsiz)
→ `int64_t` (Godot dizilerinin indeks tipi). C'deki `(int)x` yerine tercih edilir, çünkü niyeti açık ve daha güvenli.

**Yığın (stack) nesneleri**: `DIS::EntityStatePdu pdu;` fonksiyon bitince otomatik yok edilir; `new`/`delete` yok.
Bellek yönetimini sade tutmak için mümkün olduğunca böyle yazıyoruz.

## GDExtension'a özgü

**`GDCLASS(DisProbe, godot::RefCounted)`**: Godot'un sınıf sistemine kaydolmak için gereken kodu üreten makro.

**`_bind_methods()`**: Hangi C++ fonksiyonlarının GDScript'ten görüneceğini söyler.
`ClassDB::bind_static_method("DisProbe", D_METHOD("get_version"), &DisProbe::get_version)` →
GDScript'te `DisProbe.get_version()` çağrılabilir. `&DisProbe::get_version` fonksiyonun adresidir.

**`register_types.cpp` ve giriş noktası**: Godot DLL'i yükleyince `.gdextension` dosyasındaki `entry_symbol`
(`godot_dis_library_init`) adlı fonksiyonu çağırır. Bu fonksiyon başlatma/kapanış fonksiyonlarımızı kaydeder;
başlatmada `GDREGISTER_CLASS(DisProbe)` ile sınıflar Godot'a tanıtılır.

**`extern "C"`**: C++ derleyicisi fonksiyon adlarını "süsler" (name mangling). Godot giriş noktasını düz C adıyla
aradığı için o fonksiyon `extern "C"` bloğunda.

## DIS'e özgü

**Endianness (bayt sırası)**: DIS ağda *big-endian* (en anlamlı bayt önce) kullanır; x86 işlemciler
*little-endian*'dır. open-dis-cpp'nin `DataStream(DIS::BIG)` sınıfı yazarken çevirir. Bu yüzden 144 sayısı
pakette `00 90` olarak durur; GUT testinde `decode_u16` (little-endian okur) `0x9000` döndürür.
