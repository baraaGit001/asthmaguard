// Arabic-first strings (English second). L.of(context).t('key').
import 'package:flutter/widgets.dart';

class L {
  final bool ar;
  const L(this.ar);
  static L of(BuildContext c) => L(Localizations.localeOf(c).languageCode == 'ar');

  String t(String key) {
    final v = _s[key];
    if (v == null) return key;
    return ar ? v[0] : v[1];
  }

  String state(String s) => t('st_$s');
  String activity(String a) => t('act_$a');
}

const Map<String, List<String>> _s = {
  'app': ['أزما جارد', 'AsthmaGuard'],
  'next': ['التالي', 'Next'],
  'back': ['رجوع', 'Back'],
  'save': ['حفظ', 'Save'],
  'cancel': ['إلغاء', 'Cancel'],
  'done': ['تم', 'Done'],
  'add': ['إضافة', 'Add'],
  'delete': ['حذف', 'Delete'],
  'ok': ['حسنًا', 'OK'],
  'error': ['خطأ', 'Error'],
  // onboarding
  'welcome': ['أهلًا بك في أزما جارد', 'Welcome to AsthmaGuard'],
  'welcome_sub': [
    'سوار يراقب الكحة والنبض والأكسجين، وينبّهك وأهلك عند أزمة الربو.',
    'A wristband that watches cough, heart rate and oxygen, and alerts you and your family during an asthma attack.'
  ],
  'disclaimer': [
    'نموذج أولي لمشروع طلابي وليس جهازًا طبيًا معتمدًا. احمل بخاخك المعتاد دائمًا.',
    'Student prototype, not a certified medical device. Always carry your usual inhaler.'
  ],
  'patient_name': ['اسم المريض', 'Patient name'],
  'age': ['السن', 'Age'],
  'age_adult': ['الجهاز للبالغين (18 سنة فأكثر)', 'For adults only (18+)'],
  'doctor_name': ['اسم الطبيب', 'Doctor name'],
  'contacts': ['جهات الطوارئ', 'Emergency contacts'],
  'contacts_sub': ['هيوصلهم SMS بموقعك عند الطوارئ. جهة واحدة على الأقل.', 'They get an SMS with your location in an emergency. At least one.'],
  'contact_name': ['الاسم', 'Name'],
  'contact_phone': ['رقم الموبايل', 'Mobile number'],
  'permissions': ['الأذونات', 'Permissions'],
  'permissions_sub': [
    'البلوتوث للاتصال بالسوار، الموقع لرسالة الطوارئ، الرسائل لإرسالها تلقائيًا، والإشعارات للتنبيه.',
    'Bluetooth for the wristband, location for the emergency message, SMS to send it automatically, notifications for alerts.'
  ],
  'grant': ['السماح', 'Allow'],
  'required': ['مطلوب', 'Required'],
  'bad_phone': ['رقم غير صحيح', 'Invalid number'],
  // pair
  'pair': ['ربط السوار', 'Pair wristband'],
  'scan': ['بحث', 'Scan'],
  'scanning': ['جاري البحث…', 'Scanning…'],
  'no_devices': ['مفيش أجهزة. تأكد إن السوار شغال وقريب.', 'No devices found. Make sure the wristband is on and close.'],
  'connecting': ['جاري الاتصال…', 'Connecting…'],
  'connected': ['متصل', 'Connected'],
  'disconnected': ['غير متصل', 'Disconnected'],
  // home
  'home': ['الرئيسية', 'Home'],
  'history': ['السجل', 'History'],
  'settings': ['الإعدادات', 'Settings'],
  'doctor': ['منطقة الطبيب', 'Doctor area'],
  'hr': ['النبض', 'Heart rate'],
  'spo2': ['الأكسجين', 'Oxygen'],
  'coughs_min': ['كحات آخر دقيقة', 'Coughs last minute'],
  'battery': ['البطارية', 'Battery'],
  'measuring': ['جاري القياس…', 'measuring…'],
  'call_123': ['اتصل بالإسعاف 123', 'Call ambulance 123'],
  'sim_on': ['وضع المحاكاة شغال', 'Simulation mode is on'],
  'dose_off': ['البخ التلقائي متوقف لحد ما الطبيب يحدد الجرعات', 'Dosing is off until the doctor sets the limits'],
  'calibrating': ['جاري معايرة معدلاتك الطبيعية (أول 3 أيام)', 'Learning your normal values (first 3 days)'],
  'status_NORMAL': ['طبيعي', 'Normal'],
  'status_SUSPECT': ['اشتباه', 'Suspect'],
  'status_ATTACK': ['أزمة', 'Attack'],
  'status_EMERGENCY': ['طوارئ', 'Emergency'],
  'st_NORMAL': ['طبيعي', 'Normal'],
  'st_SUSPECT': ['اشتباه', 'Suspect'],
  'st_ALERT': ['تنبيه أزمة', 'Attack alert'],
  'st_WAIT_RAISE': ['ارفع ذراعك للبخاخ', 'Raise your arm'],
  'st_WAIT_CONFIRM': ['اضغط الزر للبخة', 'Press to puff'],
  'st_DOSE': ['جاري البخ', 'Puffing'],
  'st_MONITOR': ['مراقبة بعد البخة', 'Monitoring'],
  'st_RESOLVED': ['تحسنت', 'Resolved'],
  'st_EMERGENCY': ['طوارئ', 'Emergency'],
  'act_REST': ['راحة', 'Rest'],
  'act_LIGHT': ['حركة خفيفة', 'Light'],
  'act_EXERCISE': ['مجهود', 'Exercise'],
  // history
  'no_episodes': ['مفيش نوبات مسجلة', 'No episodes yet'],
  'episode': ['نوبة', 'Episode'],
  'duration': ['المدة', 'Duration'],
  'doses': ['البخات', 'Puffs'],
  'min_spo2': ['أقل أكسجين', 'Lowest SpO2'],
  'max_hr': ['أعلى نبض', 'Highest HR'],
  'emergency_sent': ['تم إرسال طوارئ', 'Emergency sent'],
  'false_alarm': ['إنذار كاذب', 'False alarm'],
  'ongoing': ['مستمرة', 'Ongoing'],
  'yes': ['نعم', 'Yes'],
  'no': ['لا', 'No'],
  'events': ['الأحداث', 'Events'],
  'min': ['د', 'min'],
  // doctor
  'pin': ['الرقم السري', 'PIN'],
  'pin_set': ['اختر رقم سري للطبيب (4 أرقام أو أكثر)', 'Choose a doctor PIN (4+ digits)'],
  'pin_enter': ['أدخل الرقم السري للطبيب', 'Enter the doctor PIN'],
  'pin_wrong': ['رقم سري غلط', 'Wrong PIN'],
  'dose_limits': ['حدود الجرعات', 'Dose limits'],
  'dose_limits_sub': [
    'البخ التلقائي يفضل متوقف لحد ما تحدد الحدّين. صفر في الاتنين يوقفه تاني.',
    'Dosing stays off until both are set. Zero in both turns it off again.'
  ],
  'dose_both': ['لازم تحدد الحدّين مع بعض', 'Set both limits together'],
  'hours': ['ساعات', 'hours'],
  'thresholds': ['الحدود والتوقيتات', 'Thresholds & timing'],
  'baselines': ['المعدلات الأساسية', 'Baselines'],
  'calibrated_at': ['تاريخ المعايرة', 'Calibrated on'],
  'not_calibrated': ['لسه ما اتعملتش معايرة', 'Not calibrated yet'],
  'export_pdf': ['تصدير تقرير PDF', 'Export PDF report'],
  'from': ['من', 'From'],
  'to': ['إلى', 'To'],
  'saved': ['اتحفظ على السوار', 'Saved to the wristband'],
  'need_connection': ['لازم السوار يكون متصل', 'The wristband must be connected'],
  // settings
  'test_alert': ['تجربة التنبيه', 'Test alert'],
  'test_servo': ['تجربة الموتور', 'Test servo'],
  'test_servo_warn': ['هيضغط البخاخ فعلًا! شيل العبوة الأول.', 'This really presses the inhaler! Remove the canister first.'],
  're_pair': ['إعادة الربط', 'Re-pair'],
  'language': ['اللغة', 'Language'],
  'simulation': ['المحاكاة', 'Simulation'],
  'simulation_sub': ['للتجربة فقط: يغيّر القيم اللي بيشوفها السوار', 'Testing only: overrides what the wristband sees'],
  // emergency
  'emergency_title': ['طوارئ — أزمة ربو', 'Emergency — asthma attack'],
  'sms_sent_to': ['اتبعتت رسالة لـ', 'SMS sent to'],
  'sms_failed': ['فشل إرسال الرسالة', 'SMS failed'],
  'sending_sms': ['جاري إرسال رسائل الطوارئ…', 'Sending emergency SMS…'],
  'late_emergency': ['وصل تنبيه طوارئ قديم (الموبايل كان بعيد) واتسجل بدون إرسال', 'An old emergency arrived late (phone was away); logged, not sent'],
  'close': ['إغلاق', 'Close'],
};
