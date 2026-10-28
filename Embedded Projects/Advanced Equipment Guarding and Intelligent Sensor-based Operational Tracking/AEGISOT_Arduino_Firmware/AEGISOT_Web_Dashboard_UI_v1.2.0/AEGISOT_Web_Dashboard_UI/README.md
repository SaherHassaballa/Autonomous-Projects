# AEGISOT Web Dashboard + USB Serial + CSV + MQTT

دليل تشغيل النسخة `v1.2.0` خطوة بخطوة على Ubuntu. هذا المشروع يضيف واجهة ويب محلية ووسيط Python يقرأ بيانات الـ ESP32 من USB Serial، يحفظ تسجيلات CSV، ويمكنه **اختياريًا** نشر البيانات على MQTT/HiveMQ. لا يوجد MQTT داخل الـ Firmware، ولا يتم استقبال أوامر تحكم من MQTT.

## 1. مكونات المشروع

| الملف | وظيفته |
|---|---|
| `aegisot_web_bridge.py` | البرنامج الرئيسي: يفتح منفذ ESP32، يقرأ Serial، يشغّل خادم الويب، يحفظ CSV ويرسل أوامر لوحة التحكم إلى الـ Firmware عبر USB فقط. |
| `index.html` | واجهة الويب لعرض الحساسات والمراحل والتسجيل والتحكم المحلي. |
| `mqtt_publisher.py` | طبقة MQTT اختيارية تنشر البيانات التي قرأها الـ Bridge؛ لا تفتح Serial ولا تشترك في Topics. |
| `mqtt_config.example.json` | نموذج إعدادات آمن بدون بيانات دخول حقيقية. انسخه إلى `mqtt_config.json` لتفعيله. |
| `requirements.txt` | المتطلبات الأساسية للتشغيل المحلي دون MQTT. |
| `requirements-mqtt.txt` | يضيف مكتبة Paho MQTT المطلوبة للنشر الاختياري. |
| `firmware_reference/` | نسخة مرجعية من Firmware اختبار الموتور والحساسات المتوافق مع بروتوكول CSV/Serial. |
| `MQTT_TOPICS.md` | ملخص Topics ورسائل MQTT. |

## 2. المتطلبات

- Ubuntu مع Python 3.
- ESP32 DevKit V1 / ESP32-WROOM-32 موصل USB.
- Firmware اختبار AEGISOT المتوافق مع رسائل `LOG_STARTED`, `CSV_DATA` و`LOG_STOPPED`.
- المتصفح على نفس الكمبيوتر.
- MQTT اختياريًا: حساب HiveMQ Cloud أو MQTT Broker متاح، وبيانات الاتصال الخاصة به.

**مهم:** لا تشغّل Arduino Serial Monitor أو `keyboard_motor_test.py` أو أي برنامج آخر يفتح `/dev/ttyACM0` في نفس الوقت. يجب أن يكون `aegisot_web_bridge.py` هو البرنامج الوحيد الذي يمسك منفذ ESP32 أثناء استعمال لوحة الويب.

## 3. فك ضغط المشروع

إذا كان ملف ZIP موجودًا في مجلد Downloads:

```bash
mkdir -p ~/Projects/AEGISOT
unzip ~/Downloads/AEGISOT_Web_Dashboard_UI_v1.2.0.zip -d ~/Projects/AEGISOT
cd ~/Projects/AEGISOT/AEGISOT_Web_Dashboard_UI
```

إذا حفظت الملف في مكان آخر، غيّر المسار فقط. تأكد أنك داخل المجلد الذي يحتوي على `aegisot_web_bridge.py`:

```bash
pwd
ls
```

## 4. اعرف منفذ ESP32

وصل البوردة ثم نفّذ:

```bash
ls -l /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
```

يمكن عرض تفاصيل المنافذ أيضًا بعد تثبيت PySerial:

```bash
python -m serial.tools.list_ports -v
```

في جهازك ذكرت أن المنفذ هو `/dev/ttyACM0`؛ استخدمه في الأوامر أدناه. إذا تغير المنفذ بعد فصل USB وإعادة توصيله، استخدم اسم المنفذ الجديد.

### إذا ظهر `Permission denied`

```bash
sudo usermod -aG dialout "$USER"
```

بعد ذلك سجّل الخروج من Ubuntu ثم ادخل مرة أخرى، وأعد المحاولة. لا تستخدم `sudo` لتشغيل البرنامج كحل دائم؛ الأفضل إصلاح صلاحية المجموعة.

## 5. ارفع الـ Firmware المتوافق (إذا لم يكن موجودًا بالفعل)

النسخة المرجعية موجودة هنا:

`firmware_reference/AEGISOT_Sensor_Motor_Test/AEGISOT_Sensor_Motor_Test.ino`

في Arduino IDE:

1. افتح ملف `.ino` من المسار أعلاه.
2. اختر لوحة ESP32 المناسبة والمنفذ الصحيح.
3. ثبت المكتبات التي يستخدمها الملف إن لم تكن مثبتة: `OneWire`, `DallasTemperature`, `Adafruit MPU6050`, `Adafruit Unified Sensor`, `Adafruit INA219`.
4. نفّذ Verify/Compile ثم Upload.
5. بعد الرفع، أغلق Serial Monitor قبل تشغيل Python.

إذا كان عندك Firmware أحدث تم اختباره ويصدر نفس بروتوكول Serial، لا تستبدله لمجرد وجود النسخة المرجعية داخل ZIP. يجب أن يصدر `LOG_STARTED`, وصفوف `CSV_DATA` بعدد الحقول المتوقع، و`LOG_STOPPED`.

**ملاحظة مهمة عن RPM:** النسخة المرجعية المرفقة في هذا ZIP فيها `ENCODER_COUNTS_PER_OUTPUT_REV = 0`، لذلك تعرض RPM كغير صالح/غير مهيأ. إذا كنت قد عدّلت نسختك المحلية إلى `5760`، فهذا التعديل غير موجود تلقائيًا في النسخة المرجعية المرفقة. استخدم قيمة CPR فقط بعد التأكد من مواصفات الإنكودر ونسبة التروس؛ وجود رقم غير صفري يجعل الحساب يعمل لكنه لا يثبت أن الرقم صحيح.

## 6. شغّل الواجهة بدون MQTT أولًا

هذه أفضل خطوة أولى للتأكد من أن الاتصال التسلسلي والتسجيل يعملان قبل إضافة الشبكة.

أنشئ بيئة Python افتراضية وثبّت المتطلبات الأساسية:

```bash
cd ~/Projects/AEGISOT/AEGISOT_Web_Dashboard_UI
python3 -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install -r requirements.txt
```

إذا لم تكن حزمة `venv` متاحة:

```bash
sudo apt update
sudo apt install python3-venv
```

ثم أعد أوامر إنشاء البيئة وتفعيلها.

شغّل الـ Bridge باستخدام المنفذ الذي عندك:

```bash
python aegisot_web_bridge.py --serial-port /dev/ttyACM0
```

إذا كان المنفذ `/dev/ttyUSB0` استخدمه بدلًا منه. السرعة التسلسلية الافتراضية في البرنامج هي `115200 baud`.

افتح المتصفح على:

**http://127.0.0.1:8765**

اترك نافذة Terminal مفتوحة طوال التشغيل؛ هي تعرض رسائل بدء البرنامج والأخطاء.

### تأكد من أن الاتصال حقيقي

- يجب أن تعرض الواجهة أن Serial متصل، ولا تظل القراءات في حالة انتظار.
- يمكن فحص حالة الـ API في Terminal ثانية:

```bash
curl -s http://127.0.0.1:8765/api/state | python -m json.tool
```

- يجب أن تكون `serial_connected` مساوية لـ `true`، وأن تتغير قيم الحساسات عند وصول القياسات.
- هذه الواجهة لا تستخدم قيم Demo عند تشغيل الـ Bridge الحقيقي؛ عدم وجود بيانات يجب أن يظهر كبيانات غير متاحة، لا كقراءات مخترعة.

## 7. طريقة تسجيل خمس دقائق لكل مرحلة

الهدف المحدد في المشروع هو **5 دقائق، بمعدل 5 Hz، أي 1500 عينة لكل مرحلة**. الوصول إلى 1500 عينة لا يوقف التسجيل تلقائيًا.

1. من لوحة الويب اضغط `Choose stage`.
2. اختر المرحلة المطلوبة: `Normal`, `Overheated`, `Vibration issue` أو `RPM issue`.
3. اضغط `Confirm & start`.
4. انتظر تأكيد الـ ESP32 `LOG_STARTED`؛ عند وصوله فقط تتحول حالة التسجيل إلى Active ويبدأ العداد.
5. اترك التسجيل لمدة خمس دقائق. راقب الوقت وعدد العينات الفعلية.
6. اضغط `Stop recording` بعد خمس دقائق وانتظر تأكيد `LOG_STOPPED`.
7. راجع عدد العينات؛ الهدف 1500 تقريبًا. إذا أوقفت التسجيل مبكرًا، سيسجل الملخص المدة وعدد الصفوف الفعلي.
8. كرر بنفسك للمرحلة التالية. لا توجد مرحلة تبدأ تلقائيًا.

ملفات البيانات والملخص تُحفظ في:

`~/AEGISOT_Data/`

وتظهر بأسماء شبيهة بـ:

- `aegisot_web_dataset_YYYYMMDD_HHMMSS.csv`
- `aegisot_web_summary_YYYYMMDD_HHMMSS.csv`

يمكن تنزيل ملف الجلسة من زر `Download session CSV` في الواجهة. لا يوقف `Stop recording` الموتور؛ استخدم زر `STOP motor` بشكل منفصل لإيقافه.

### التحكم في الموتور من الواجهة

أزرار السرعة والاتجاه ترسل نفس أوامر الاختبار التسلسلي إلى Firmware: زيادة أو تقليل السرعة بمقدار 5%، اختيار `Forward` أو `Reverse`، إيقاف الموتور، تصفير الـ Encoder (عند التوقف فقط)، ومسح Fault إذا سمحت شروط السلامة بذلك. تغيير الاتجاه ينفذه Firmware بعد إيقاف الموتور أولًا.

ابدأ بسرعة منخفضة، واتبع متطلبات سلامة الموتور ومصدر الطاقة. لا تتجاوز حد التيار أو تعطل شروط الحماية لمجرد أن أمرًا لم يُقبل.

## 8. فعّل MQTT / HiveMQ (اختياري)

MQTT **غير مفعّل** عند تشغيل الأمر في القسم 6. لتفعيل النشر، اترك الـ Firmware كما هو، وأضف إعدادات MQTT في Python فقط.

### 8.1 ثبّت مكتبة MQTT

من نفس مجلد المشروع وبعد تفعيل البيئة الافتراضية:

```bash
python -m pip install -r requirements-mqtt.txt
```

### 8.2 أنشئ ملف إعدادات خاصًا بك

```bash
cp mqtt_config.example.json mqtt_config.json
nano mqtt_config.json
```

املأ الحقول التالية ببيانات Broker الفعلية:

- `enabled`: اتركه `true` لتفعيل النشر.
- `host`: اسم Host من تفاصيل HiveMQ Cloud، بدون `https://` وبدون مسار URL.
- `port`: استخدم `8883` لاتصال HiveMQ Cloud عبر TLS إذا كانت هذه هي إعدادات مثيلك.
- `username` و`password`: بيانات مستخدم MQTT التي أنشأتها للـ Broker، وليست بالضرورة بيانات تسجيل دخول حساب HiveMQ على الويب.
- `client_id`: اسم فريد لهذا البرنامج؛ استخدم قيمة مختلفة إذا شغلت أكثر من Publisher.
- `device_id`: اسم الجهاز الذي سيظهر في الرسائل.
- `base_topic`: بادئة الـ Topics؛ لا تستخدم `+` أو `#` هنا.
- `tls`: اتركها `true` حتى يظل التحقق من شهادة TLS مفعلًا.
- `qos`: القيمة في المثال `0`، أي نشر بأفضل جهد. يمكن اختيار `1` إذا أردت تسليمًا مؤكدًا على مستوى MQTT.
- `publish_idle_interval_seconds`: فترة نشر البيانات أثناء عدم التسجيل (الافتراضي ثانية واحدة).
- `publish_recording_interval_seconds`: فترة النشر أثناء التسجيل (الافتراضي 0.2 ثانية، أي حتى 5 مرات في الثانية).
- `retain_status`: الاحتفاظ بآخر حالة للجهاز.
- `retain_telemetry`: الاحتفاظ بآخر Snapshot للـ telemetry؛ الافتراضي `false`.
- `retain_sensor_topics`: الاحتفاظ بآخر قراءات كل Sensor topic؛ الافتراضي `false`.

مثال على بادئة الـ Topics الافتراضية:

`factory/devices/aegisot-node-01`

**الأمان:** لا تشارك `mqtt_config.json` ولا ترفع ملفه إلى Git إذا كان يحتوي على كلمة المرور. ملف `.example.json` هو القالب الذي يمكن مشاركته بعد التأكد من عدم وجود أسرار.

### 8.3 شغّل الـ Bridge مع MQTT

```bash
python aegisot_web_bridge.py \
  --serial-port /dev/ttyACM0 \
  --mqtt-config mqtt_config.json
```

ثم افتح نفس الصفحة:

**http://127.0.0.1:8765**

لا تشغّل نسخة ثانية من الـ Bridge بالتوازي؛ تشغيل نسختين قد يتسبب في تعارض منفذ Serial أو استخدام `client_id` نفسه.

### 8.4 تأكد من الاتصال والنشر

افحص `/api/state`:

```bash
curl -s http://127.0.0.1:8765/api/state | python -m json.tool
```

ابحث عن كائن `mqtt` وتحقق من:

- `enabled: true`
- `connected: true`
- أن `base_topic` هو المسار الذي اخترته.
- أن `last_error` فارغ. إذا ظهر خطأ، عالجه قبل اعتبار MQTT متصلًا.

كما تظهر حالة وأحداث MQTT في سجل الواجهة. لمشاهدة الرسائل من جهاز آخر، استخدم MQTT Explorer أو عميل MQTT آخر بنفس بيانات الـ Broker، فعّل TLS، ثم اشترك في:

`factory/devices/aegisot-node-01/#`

إذا غيّرت `base_topic`، استخدم المسار الذي اخترته بدل هذا المثال.

## 9. Topics التي ينشرها البرنامج

كل الرسائل Payload بصيغة JSON، وتشمل `device_id` ووقت UTC، و`schema_version` حيث ينطبق. القيم غير المتاحة تُرسل `null` بدلًا من تخمينها.

| Topic الافتراضي | المحتوى |
|---|---|
| `factory/devices/aegisot-node-01/telemetry` | Snapshot كامل من القراءات والبيانات الوصفية للتسجيل. |
| `factory/devices/aegisot-node-01/status` | حالة Serial والتسجيل والمرحلة والموتور والأعطال. |
| `factory/devices/aegisot-node-01/motor` | حالة الموتور والاتجاه والسرعة وPWM والتيار والجهد وFault. |
| `factory/devices/aegisot-node-01/sensors/ina219` | الجهد والتيار والقدرة وصلاحية القياس. |
| `factory/devices/aegisot-node-01/sensors/temperature` | حرارة DS18B20 وصلاحية القراءة. |
| `factory/devices/aegisot-node-01/sensors/mpu6050` | التسارع والجيروسكوب ومؤشر الاهتزاز وصلاحية القياس. |
| `factory/devices/aegisot-node-01/sensors/encoder` | العداد والفرق ومعدل العد وCPR وRPM وصلاحيته. |
| `factory/devices/aegisot-node-01/recording` | المرحلة الحالية والزمن وعدد العينات والهدف وعدّادات المراحل. |
| `factory/devices/aegisot-node-01/events` | أحداث بدء/إيقاف التسجيل والاتصال والأخطاء. |
| `factory/devices/aegisot-node-01/availability` | `online` و`offline` retained status. |

فترات النشر: نحو 1 Hz أثناء الانتظار، وحتى 5 Hz أثناء تسجيل CSV. هذا توقيت نشر MQTT، أما أخذ عينات CSV في الـ Firmware فيبقى 5 Hz مستقلًا عنه.

**MQTT للنشر فقط:** البرنامج لا يشترك في أي Topics ولا يقبل أوامر تشغيل/إيقاف الموتور من MQTT. التحكم والتسجيل يظلان من صفحة الويب عبر Python Bridge ثم USB Serial إلى الـ ESP32.

## 10. التشغيل اليومي بعد الإعداد الأول

### بدون MQTT

```bash
cd ~/Projects/AEGISOT/AEGISOT_Web_Dashboard_UI
source .venv/bin/activate
python aegisot_web_bridge.py --serial-port /dev/ttyACM0
```

### مع MQTT

```bash
cd ~/Projects/AEGISOT/AEGISOT_Web_Dashboard_UI
source .venv/bin/activate
python aegisot_web_bridge.py --serial-port /dev/ttyACM0 --mqtt-config mqtt_config.json
```

أبقِ Terminal مفتوحًا. لإيقاف البرنامج اضغط `Ctrl+C` في Terminal. أثناء الإغلاق، يحاول الـ Bridge إيقاف التسجيل إذا كان نشطًا، ثم يرسل أمر STOP للموتور قبل إغلاق المنفذ. افحص نتيجة الإغلاق ولا تعتمد على ذلك بدلًا من زر STOP عند الحاجة إلى إيقاف فوري.

## 11. حل المشاكل الشائعة

### `Permission denied` على `/dev/ttyACM0`

أضف المستخدم إلى `dialout` كما في القسم 4، ثم سجّل الخروج والدخول من Ubuntu.

### `Device or resource busy` أو المنفذ غير متاح

- أغلق Arduino Serial Monitor.
- أوقف `keyboard_motor_test.py` وأي Bridge آخر.
- تحقق من المنفذ بالأمر `python -m serial.tools.list_ports -v`.

### الموقع يفتح لكن لا توجد قراءات

- راجع مؤشر Serial في الموقع.
- تحقق من المنفذ الصحيح و`115200 baud`.
- تأكد أن الـ Firmware يرسل رسائل Telemetry النصية وصفوف `CSV_DATA` بنفس ترتيب الحقول المتوقع.
- افتح `/api/state` وراجع `serial_error`.

### التسجيل لا يبدأ

- يجب أن يكون Serial متصلًا.
- يبدأ المؤقت بعد وصول `LOG_STARTED` من الـ ESP32؛ مجرد الضغط على الزر ليس تأكيدًا.
- إذا لم يصل التأكيد خلال 5 ثوانٍ، راجع Firmware والـ Serial/Terminal.
- تأكد أنك رفعت Firmware المتوافق وليس Firmware MQTT مختلفًا بصيغة Serial أخرى.

### MQTT لا يتصل

- تأكد أنك ثبّت `requirements-mqtt.txt` داخل نفس البيئة الافتراضية التي تشغّل البرنامج بها.
- راجع Host وPort واسم المستخدم وكلمة المرور.
- يجب أن يكون `host` اسم الخادم فقط، وTLS مفعّلًا.
- تحقق من الإنترنت وصلاحيات المستخدم للنشر إلى المسارات المطلوبة.
- راجع `mqtt.last_error` في `/api/state` وسجل الواجهة.
- لا تستخدم `tls: false` كحل سريع، ولا تعطل التحقق من الشهادة.

### MQTT متصل لكن لا ترى الرسائل

- اشترك في `factory/devices/aegisot-node-01/#` أو البادئة التي أدخلتها في `base_topic`.
- تحقق أن تطبيق العميل يتصل بنفس الـ Cluster/Host ونفس الحساب.
- رسائل Telemetry لا تُحفظ retained افتراضيًا؛ افتح الاشتراك بينما الـ Bridge يعمل، وانتظر دورة نشر جديدة.

### RPM فارغ أو غير صالح

يلزم أن تكون قيمة `ENCODER_COUNTS_PER_OUTPUT_REV` صحيحة ومؤكدة في الـ Firmware، وأن يرسل الـ Serial الحقول `encoder_counts_per_revolution`, `rpm_valid`, `rpm`. القيمة غير الصفرية وحدها لا تثبت صحة RPM. النسخة المرجعية في هذا ZIP تحتوي CPR=0 إلى أن يُضبط العدد الصحيح؛ راجع التنبيه في القسم 5.

## 12. السلامة والحدود

- ابدأ باختبار الموتور بسرعة منخفضة وبمصدر طاقة وتوصيلات مناسبة.
- لا تجعل MQTT يرسل أوامر تحكم؛ هذه الإضافة تنشر القياسات فقط.
- زر إيقاف التسجيل لا يوقف الموتور. استخدم STOP motor منفصلًا.
- Firmware هو المسؤول عن شروط تشغيل الموتور والحماية الموجودة فيه؛ لا يتجاوز الـ Bridge هذه الشروط.
- مؤشر الاهتزاز المعروض تقدير برمجي من التسارع، وليس تشخيصًا صناعيًا معايرًا.
- لا تُصنّف مرحلة مثل Overheated أو Vibration issue على أنها عطل حقيقي إلا إذا كانت طريقة إحداث الحالة مقصودة وموثقة وآمنة.
- تمت مراجعة صياغة Python واختبار واجهة API باستخدام Serial غير متصل، لكن لم يتم إجراء اختبار مادي على ESP32 في بيئة إعداد هذا الدليل. يلزم اختبار فعلي قبل الاعتماد على النتائج.
