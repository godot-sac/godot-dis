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

## Faz 1: PDU sınıfları

**Global değişkenler ve DLL yükleme sırası (yaşanmış hata).** `thread_local String last_error;` gibi bir global
`godot::String`, DLL yüklenir yüklenmez, yani Godot godot-cpp'yi başlatmadan *önce* oluşturulur. `godot::String`
oluşturulurken Godot'un API'sini çağırdığı için yükleme "DLL initialization routine failed" hatasıyla çöktü.
Kural: global/static değişkenlerde Godot tipleri (`String`, `Array`, `Ref`...) kullanma; `std::string` gibi saf C++
tipleri kullan ya da değeri ilk kullanımda oluştur. (`src/pdu/dis_pdu.cpp`)

**`thread_local`**: Her thread'in kendi kopyası olan değişken. Ağ alma thread'i (Faz 2) ile ana thread birbirinin
hata mesajını ezmesin diye.

**İsimsiz namespace (`namespace { ... }`)**: İçindeki fonksiyonlar yalnızca o `.cpp` dosyasında görünür (C'deki
`static` fonksiyon). Yardımcı fonksiyonlar (`read_u16_be`, `to_dis` ...) böyle saklanıyor.

**Eksik tip hatası (`use of undefined type 'godot::Ref<...>'`)**: Bir başlık bir sınıfı yalnızca *ileri bildirmişse*
(forward declaration: "böyle bir sınıf var" der ama içini göstermez), o sınıfın nesnesini oluşturamazsın.
Çözüm: tam tanımı içeren başlığı eklemek (`#include <godot_cpp/classes/ref.hpp>`).

**`Ref<T>`**: Godot'un referans sayan akıllı işaretçisi. `RefCounted`'dan türeyen nesneler `Ref` ile tutulur, son `Ref`
yok olunca nesne silinir. `out.instantiate()` yeni nesne oluşturur, `Ref<T>()` boş (null) referanstır.

**`constexpr`**: Derleme zamanında bilinen sabit (`static constexpr int BASE_SIZE = 144;`). `#define`'dan daha
güvenlidir çünkü tipi vardır ve namespace kurallarına uyar.

**Makro ile tekrar azaltma**: `DIS_PROPERTY(VECTOR3I, entity_id)` her özellik için aynı üç satırı (setter bağla,
getter bağla, `ADD_PROPERTY`) üretir. `#name` makro parametresini metne, `set_##name` iki parçayı tek isme çevirir.
Makrolar hata ayıklamayı zorlaştırır; yalnızca böyle mekanik tekrarlarda ve dosya içinde (`#undef` ile) kullanıyoruz.

**`reinterpret_cast`**: Bir işaretçiyi bambaşka bir tip gibi görmek (`const uint8_t*` → `const char*`). Bitleri
değiştirmez, yalnızca derleyiciye "bu belleği böyle oku" der. Sadece bayt tamponları gibi bilinçli yerlerde.

**`ERR_FAIL_COND_MSG(koşul, mesaj)`**: godot-cpp makrosu: koşul doğruysa Godot'a hata yazar ve fonksiyondan çıkar.
C++ exception yerine Godot tarzı hata yönetimi (godot-cpp exception'ları kapalı derler).

**Güvenilmeyen girdi**: Ağdan gelen bayt dizisine asla güvenme. open-dis-cpp okurken `std::vector::at()` kullanıyor;
exception'lar kapalı olduğu için sınır dışı okuma programı *sonlandırır*. Bu yüzden `DisPdu::validate()` uzunlukları
open-dis-cpp'ye vermeden önce kontrol ediyor ve testler 2000 bozuk paketle bunu deniyor.

## Faz 2: Ağ düğümü

**`Node`'dan türeyen C++ sınıfı ve `_notification`**: GDScript'teki `_ready()` / `_process()` C++'ta tek bir
`_notification(int p_what)` fonksiyonunda `switch` ile yakalanır (`NOTIFICATION_READY`, `NOTIFICATION_PROCESS`,
`NOTIFICATION_EXIT_TREE`). `_process` bildirimlerinin gelmesi için `set_process(true)` gerekir.
`Engine::get_singleton()->is_editor_hint()` editörde soket açmayı engeller. (`src/network/dis_network.cpp`)

**Sinyaller**: `ADD_SIGNAL(MethodInfo("pdu_received", PropertyInfo(...), ...))` sinyali tanımlar,
`emit_signal("pdu_received", pdu, ip, port)` yayar. GDScript'te `network.pdu_received.connect(...)` ile dinlenir.

**C++'tan Godot sınıflarını kullanmak**: godot-cpp, Godot'un kendi sınıflarını da (`PacketPeerUDP`, `IP`) C++'a açar.
Ağ kodunu sıfırdan yazmak yerine Godot'unkini kullandık: broadcast/multicast her platformda hazır.

**`Ref` ile tip dönüşümü**: `const Ref<DisEntityStatePdu> entity_state = pdu;` → `pdu` gerçekten o tipteyse geçerli,
değilse boş (null) olur. GDScript'teki `if pdu is DisEntityStatePdu` kontrolünün C++ karşılığı.

**`Variant` ile dinamik çağrı**: `pdu->call("encode")` ve `pdu->get("exercise_id")` derleme zamanında tipi
bilinmeyen bir Godot nesnesinin metodunu/özelliğini adıyla çağırır (GDScript gibi). `send_pdu` gelecekteki tüm
PDU sınıflarıyla çalışsın diye böyle.

**Aralık tabanlı `for`**: `for (const String &name : names)` dizinin her elemanını sırayla verir
(GDScript'teki `for name in names`). `const &` kopyalamayı önler.

**Değişken sayıda makro argümanı (`__VA_ARGS__`) ve MSVC**: `##__VA_ARGS__` ile boş argümanda virgülü silme
GCC/Clang uzantısıdır; MSVC'nin eski önişlemcisi bunu yapmaz ("syntax error: ')'"). Çözüm: iki ayrı makro
(`DIS_PROPERTY` ve `DIS_PROPERTY_HINT`). Taşınabilir kod yazarken derleyici farklarını akılda tut.

## Faz 3: Koordinatlar

**Godot'tan bağımsız C++ çekirdeği**: `src/geo/geo_math.{h,cpp}` hiçbir Godot başlığı içermez; yalnızca standart
kütüphane (`<cmath>`). Böylece aynı dosya hem eklentiye hem de ayrı bir test programına (`build/tests/test_geo.exe`)
derlenir. Godot'a açılan sınıflar (`DisGeo`, `DisGeoOrigin`) bu çekirdeğin üstünde ince bir katman.

**`namespace`**: `namespace dis::geo { ... }` isimleri gruplar; dışarıdan `dis::geo::geodetic_to_ecef(...)` diye
çağrılır. Aynı isimde Godot metoduyla (`DisGeo::geodetic_to_ecef`) çakışmayı böyle önlüyoruz.
`using namespace dis::geo;` bir dosyada ön eki yazmadan kullanmayı sağlar.

**`struct` ve operatör aşırı yükleme**: `struct Vec3 { double x, y, z; Vec3 operator+(const Vec3 &o) const; }`
→ `a + b` yazılabilir. `struct` ile `class` aynı şeydir; tek fark varsayılan erişimin `public` olması.
`Vec3{1, 2, 3}` (süslü parantez) alanları sırayla doldurur.

**`constexpr`**: `constexpr double WGS84_A = 6378137.0;` derleme zamanında bilinen sabit. `#define`'dan farkı:
tipi var, namespace'e uyar.

**`inline` başlık fonksiyonları**: Başlıkta gövdesiyle yazılan küçük fonksiyonlar (`enu_to_godot`) `inline` olmalı;
yoksa başlığı içeren her `.cpp` kendi kopyasını üretir ve bağlayıcı (linker) "tanım iki kez var" hatası verir.

**Üye ile metot aynı isimde olamaz**: `LocalFrame`'de hem `Mat3 enu_to_ecef;` hem `Vec3 enu_to_ecef(...)` vardı;
C++ bunu reddeder. Üyenin adı `enu_axes` oldu. (GDScript'te de aynı kural.)

**Statik metot bağlama**: `ClassDB::bind_static_method("DisGeo", D_METHOD(...), &DisGeo::geodetic_to_ecef)`
→ GDScript'te nesne oluşturmadan `DisGeo.geodetic_to_ecef(...)`. Sınıf `GDREGISTER_ABSTRACT_CLASS` ile kayıtlı,
çünkü örneği oluşturmanın anlamı yok.

**doctest ile C++ birim testleri**: `thirdparty/doctest/doctest.h` tek başlıklık bir test kütüphanesi.
`TEST_CASE("ad") { CHECK(a == b); }` yazılır; `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN` `main()`'i kendisi üretir.
`scons tests` ayrı bir SCons ortamıyla (exception'lar açık, Godot yok) derleyip çalıştırır. Godot'u açmadan
saniyeler içinde binlerce kontrol yapılır.

**`double` ve `float` farkı (neden önemli)**: `float` (32 bit) ~7 anlamlı basamak tutar. ECEF koordinatı ~6 378 137 m;
32 bitte en küçük adım 0.5 m. `double` (64 bit) ~16 basamak → nanometre. Bu yüzden ECEF her yerde `double`; Godot'un
`Vector3`'üne ancak orijinden çıkarıldıktan sonra (küçük yerel değer) dönüştürülüyor.

## Faz 4: Varlıklar

**Şablon (template)**: `template <typename T> T *find_node(Node *self, ...)` tek bir fonksiyonu her tip için ayrı
derletir: `find_node<DisNetwork>(...)` ve `find_node<DisGeoOrigin>(...)` aynı koddan iki fonksiyon olur. Şablonlar
başlıkta yazılır, çünkü derleyici kullanıldıkları yerde gövdeyi görmek zorunda. (`src/entity/scene_lookup.h`)

**`std::unordered_map`**: anahtar → değer tablosu (GDScript'teki `Dictionary`, ama tipli). Yönetici uzak varlıkları
`entity_id`'den üretilen 64 bitlik bir anahtarla tutuyor: `(site << 32) | (application << 16) | entity`.

**Yapısal bağlama (structured bindings)**: `for (const auto &[key, object_id] : entities)` her çifti iki isme açar.
`auto` tipi derleyiciye bıraktırır.

**`ObjectID` ve `ObjectDB::get_instance`**: Kullanıcı bir uzak varlık düğümünü kendisi silebilir; elimizde ham
işaretçi (`DisRemoteEntity *`) kalırsa artık var olmayan belleği gösterir (dangling pointer → çökme). Bu yüzden
haritada düğümün kimliği (`ObjectID`) saklanıyor; her kullanımda `ObjectDB::get_instance(id)` düğüm hâlâ yaşıyorsa onu,
yoksa `nullptr` döndürür. GDScript'teki `is_instance_valid()` ile aynı fikir.

**`memnew` ve `queue_free`**: Godot nesneleri C++'ta `new` ile değil `memnew(DisRemoteEntity)` ile oluşturulur
(Godot'un bellek takibi için). Ağaca eklenen düğümü Godot sahiplenir; silmek için `queue_free()`.

**`callable_mp`**: Bir C++ metodunu sinyale bağlar:
`network->connect("entity_state_received", callable_mp(this, &DisEntityManager::on_entity_state))`.
Metot `_bind_methods`'ta bağlanmak zorunda değil, GDScript'ten görünmez.

**`friend class`**: `DisRemoteEntity` içindeki `friend class DisEntityManager;` yöneticinin varlığın private
alanlarına (durum, yumuşatma ofsetleri) doğrudan erişmesine izin verir; GDScript'e açılmaz.

**İsimsiz namespace**: `.cpp` içindeki `namespace { ... }` o dosyaya özel yardımcılar tanımlar (başka dosyalar
göremez, isim çakışması olmaz). C'deki `static` fonksiyonun modern hali.

**Varsayılan argüman (`DEFVAL`)**: `handle_entity_state(pdu, now = -1.0)`; GDScript'e de varsayılanı göstermek için
`bind_method(..., DEFVAL(-1.0))`.

**Test edilebilir zaman**: Düğümler zamanı kendileri okumak yerine `process_entity(now)` / `process_entities(now)`
ile dışarıdan alabiliyor. Testler `auto_update = false` yapıp zamanı kendisi veriyor; böylece "5 saniye sonra heartbeat"
gibi durumlar beklemeden ve her seferinde aynı sonuçla denenebiliyor.

**Kayan nokta ve gönderilen durum**: Ağda hız `float` (32 bit) gider. `DisEntity`, alıcının ne gördüğünü taklit
ettiği kopyayı da `float`'a yuvarlıyor (`as_float32`); yoksa iki taraf biraz farklı tahmin yapar.

## godot-geo entegrasyonu: zemine oturtma

**Duck typing (ördek tiplemesi) C++'tan**: `DisEntityManager` zemini bir sınıf olarak bilmiyor; yalnızca
`ground->has_method("height_at")` ve `ground->call("height_at", x, z)` kullanıyor. `call` Variant üzerinden gider,
yani düğüm GDScript, C# ya da C++ olabilir; godot-dis godot-geo'ya bağımlı olmaz. Bedeli: her çağrı birkaç yüz
nanosaniye, derleme zamanı kontrolü yok (yanlış düğüme bir kez uyarı verilir).

**Lambda**: `auto height = [ground](real_t x, real_t z) { return ...; };` yerinde tanımlanan küçük bir fonksiyon.
Köşeli parantez içi "yakalanan" değişkenler: `ground` işaretçisi kopyalanır. Aynı çağrıyı beş kez yazmamak için.

**`std::isfinite`** (`<cmath>`): NAN ve sonsuzu ayıklar. `NAN == NAN` yanlıştır, bu yüzden NAN'ı `==` ile değil böyle
denetleriz.

## Faz 6: Kendi soket katmanımız, CI

**İşletim sistemi API'si ve `#ifdef`**: `src/network/udp_socket.cpp` Godot'u kullanmadan doğrudan işletim sisteminin
soket API'sini çağırır: Windows'ta Winsock (`winsock2.h`, `closesocket`, `WSAStartup`), Linux/macOS'ta BSD soketleri
(`sys/socket.h`, `close`). Farklı kısımlar `#ifdef _WIN32 ... #else ... #endif` ile ayrılır; derleyici yalnızca
hedef platformun kodunu görür. Windows'ta ayrıca `ws2_32` kütüphanesine bağlanmak gerekir (`SConstruct`).

**Neden Godot'un `PacketPeerUDP`'si değil**: Godot, Windows'ta `SO_REUSEADDR`'ı hiç açmıyor. Bu yüzden aynı
bilgisayarda iki DIS uygulaması 3000 portunu birlikte dinleyemiyordu (demo için şart). Bir kütüphanenin neyi
desteklemediğini test ederek bulduk ve gerekeni kendimiz yazdık.

**RAII ve `std::unique_ptr`**: `UdpSocket`'in yıkıcısı (`~UdpSocket()`) soketi kapatır. `DisNetwork` onu
`std::unique_ptr<UdpSocket>` ile tutar: `socket.reset()` nesneyi siler, yıkıcı soketi kapatır. Kaynağın ömrü
nesnenin ömrüne bağlı olduğu için "kapatmayı unutmak" mümkün değil (Resource Acquisition Is Initialization).

**Kopyalamayı yasaklamak**: `UdpSocket(const UdpSocket &) = delete;` İki kopya aynı işletim sistemi soketini
kapatmaya çalışırdı. `= delete` ile derleyici kopyalamaya izin vermez.

**Ağ bayt sırası**: `htons(port)` / `ntohs` portu "host" bayt sırasından ağ sırasına (big-endian) çevirir; DIS
paketlerinin kendisinin big-endian olması da aynı gelenekten.

**Zaman kaynağı hatası (headless demo ile bulundu)**: `DisEntity` hızı gerçek saatle (wall clock) ölçüyordu, ama
düğümler fizik zamanında hareket ediyor. Headless çalışırken Godot bazen iki fizik adımını aynı karede arka arkaya
yapıyor: gerçek zamanda mikrosaniyeler, konumda 1/60 s'lik hareket. Hız tahmini patlıyor, her karede PDU gidiyordu
(20 s'de 404 PDU). Artık yayıncı fizik zamanını biriktirip kullanıyor (20 s'de 80 PDU). Ders: ölçtüğün şeyle aynı
saati kullan.

**CI (GitHub Actions)**: `.github/workflows/ci.yml` her push'ta Windows, Linux ve macOS'ta derler, C++ testlerini
çalıştırır, Windows ve Linux'ta Godot'u indirip GUT testlerini koşar ve üç platformun kütüphaneleriyle tek bir
`addons/dis` paketi üretir. Linux kısmı yerelde WSL (Ubuntu 24.04, GCC 13) ile denendi.

## Godot hatasına geçici çözüm: `CallableCustom` ve yıkıcıyı kanca olarak kullanmak
`src/editor_first_load_guard.cpp`. Godot 4.7'de yeni bir projenin ilk `--headless --import`'u eklentimizi tarama
sırasında keşfedince çıkışta çöküyordu (godotengine/godot#111048): editör sınıf belgelerini ertelenmiş bir çağrıyla
üretiyor, editör hemen kapanırsa bu çağrı belge verisi silindikten sonra çalışıp boş işaretçiye (`nullptr`) erişiyor.
Hatayı minimal bir C eklentisiyle (godot-cpp'siz) yeniden üretip motorda olduğunu ayırdık; `is_exposed` kapalı bir
sınıfla çökmediğini görmek nedeni kesinleştirdi.

- **`CallableCustom`**: Godot'nun `Callable`'ını kendi C++ sınıfımızla doldurmak. Godot callable'ı bir sinyal
  bağlantısında tutar; bağlantı (ya da sinyalin sahibi olan nesne) yok olunca callable'ı siler ve bizim yıkıcımız
  çalışır. Burada çağrının kendisi önemsiz: kök pencerenin `renamed` sinyaline bağlıyoruz ki kök pencere silinince,
  yani editör düğümü ve belge verisi gittikten sonra, yıkıcı çalışsın ve sınıflarımızı erkenden kaldırsın.
- **Yıkıcı = "silinme olayı"**: RAII'nin tersten kullanımı; bir nesnenin ömrünü başka bir nesneye bağlayıp onun
  silinişinden haberdar olmak.
- **Anonim ad alanı (`namespace { ... }`)**: içindekiler yalnızca o `.cpp` dosyasında görünür (C'deki `static` gibi);
  başka dosyalarla isim çakışması olmaz.
- **`is_valid()` dersi**: hedef nesnesi olmayan callable varsayılan olarak "geçersiz" sayılıyor, `connect()` onu
  reddedip hemen siliyor. İlk denemede bu yüzden sahte bir başarı gördük; günlükte kancanın ne zaman çalıştığına bakmak
  ortaya çıkardı. Ders: düzeltmenin *neden* çalıştığını doğrula.

## v0.1 PDU'ları, kayıt ve oynatma (Faz 7–10)

- **Sanal fonksiyon ve `override`** (`src/pdu/dis_pdu_base.h`): `DisPduBase::encode()` `virtual`; her PDU sınıfı
  kendi `encode() const override`'unu yazar. `DisNetwork` elinde `Ref<DisPduBase>` tutup `pdu->get_pdu_type()`
  çağırdığında, nesnenin gerçek sınıfının fonksiyonu çalışır (dinamik dağıtım). `override` yazmak zorunlu değil ama
  imza tutmazsa derleyici hata verir; yazım hatasıyla yeni bir fonksiyon açmanın önüne geçer. Godot'a
  `bind_method(&DisPduBase::encode)` bir kez bağlanır; sanal olduğu için GDScript'ten çağrı da doğru alt sınıfa gider.
- **Şablon (template)** (`checked<Pdu>()`, `dis::find_first_of_class<T>()`): aynı kodu farklı tipler için bir kez
  yazmak. Derleyici her kullanılan tip için ayrı bir kopya üretir; tip denetimi derleme anında yapılır.
- **Çoklu kalıtım ve arayüz** (`DisRecorder : public godot::Node, public DisPacketTap`): `DisPacketTap` yalnızca saf
  sanal bir fonksiyonu (`= 0`) olan bir "arayüz". `DisNetwork` kayıt düğümünü tanımak zorunda kalmaz, sadece
  `DisPacketTap*` görür; bu da `network/` klasörünün `record/`'a bağımlı olmasını önler.
- **Ham işaretçi + `ObjectID`**: `DisNetwork` dinleyicileri `(ObjectID, DisPacketTap*)` çifti olarak tutar ve her
  çağrıdan önce `ObjectDB::get_instance(id)` ile nesnenin hâlâ yaşadığını denetler. Godot nesneleri bizim
  bilmediğimiz bir anda silinebilir; ham işaretçi tek başına "sarkan işaretçi" (dangling pointer) riski taşır.
- **Lambda ve STL algoritmaları** (`std::stable_sort`, `std::lower_bound`, `std::remove_if`): `[](const Packet &a,
  const Packet &b) { return a.time < b.time; }` yerinde yazılmış küçük bir fonksiyon. `stable_sort` eşit zamanlı
  paketlerin sırasını korur; `lower_bound` sıralı dizide ikili arama yapar (`seek()` binlerce paket arasında anında).
- **Yapılandırılmış bağlama** (`for (const auto &[key, index] : latest)`): bir `std::pair`'i iki isme açar (C++17).
- **Bayt sırası, elle**: pcapng dosyası yazıldığı makinenin bayt sırasını taşır (bölüm başlığındaki 0x1A2B3C4D
  "sihirli sayı" ters okunursa dosya big-endian'dır). `Reader` yapısı her okumayı gerekirse ters çevirir; DIS ve
  IP/UDP başlıkları ise her zaman big-endian (ağ sırası).
- **İnternet sağlama toplamı** (`internet_checksum`): 16 bitlik kelimelerin taşmalı toplamı, sonra tersi. Doğru bir
  IPv4 başlığının toplamı 0 çıkar; test bunu kullanır. UDP toplamı başına adresleri içeren bir "sözde başlık" ekler.
- **Godot'tan bağımsız çekirdek**: `capture_format.cpp` yalnızca `std::vector<uint8_t>` ve `std::string` kullanır,
  bu yüzden doctest programına doğrudan girer; Godot'ya özgü dosya erişimi (`FileAccess`, `user://`) üstteki ince
  sınıflarda (`DisRecording`, `DisRecorder`). Aynı ayrım `geo_math` ve `dead_reckoning`'de de var.
- **MSVC ile GCC farkı**: `std::all_of` için `<algorithm>` eklemeyi unutmuştuk; MSVC başka bir başlık üzerinden
  dolaylı olarak buluyordu, GCC bulmadı. Ders: kullandığın her şeyin başlığını kendin ekle; Linux derlemesi bu tür
  hataları yakalar.
