import 'package:flutter/material.dart';
import 'package:flutter_foreground_task/flutter_foreground_task.dart';
import 'package:flutter_localizations/flutter_localizations.dart';

import 'app_controller.dart';
import 'data/sqlite_store.dart';
import 'services/device_service.dart';
import 'services/platform.dart';
import 'ui/emergency_screen.dart';
import 'ui/history_screen.dart';
import 'ui/home_screen.dart';
import 'ui/onboarding_screen.dart';
import 'ui/pair_screen.dart';
import 'ui/scope.dart';
import 'ui/settings_screen.dart';
import 'l10n.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  FlutterForegroundTask.initCommunicationPort();
  final store = await SqfliteStore.open();
  final c = AppController(store: store, device: DeviceService(), platform: PlatformServices());
  await c.init();
  runApp(AgApp(controller: c));
}

const seed = Color(0xFF0E7C86);

class AgApp extends StatefulWidget {
  final AppController controller;
  const AgApp({super.key, required this.controller});
  @override
  State<AgApp> createState() => _AgAppState();
}

class _AgAppState extends State<AgApp> {
  bool _emergencyOpen = false;

  @override
  void initState() {
    super.initState();
    widget.controller.showEmergency = (res, e) {
      if (_emergencyOpen) return; // the open screen listens to the controller
      final nav = widget.controller.navigatorKey.currentState;
      if (nav == null) return;
      _emergencyOpen = true;
      nav
          .push(MaterialPageRoute(fullscreenDialog: true, builder: (_) => EmergencyScreen(event: e)))
          .whenComplete(() => _emergencyOpen = false);
    };
  }

  @override
  Widget build(BuildContext context) {
    final c = widget.controller;
    return AppScope(
      controller: c,
      child: ListenableBuilder(
        listenable: c,
        builder: (context, _) => MaterialApp(
          title: 'AsthmaGuard',
          debugShowCheckedModeBanner: false,
          navigatorKey: c.navigatorKey,
          locale: c.locale,
          supportedLocales: const [Locale('ar'), Locale('en')],
          localizationsDelegates: const [
            GlobalMaterialLocalizations.delegate,
            GlobalWidgetsLocalizations.delegate,
            GlobalCupertinoLocalizations.delegate,
          ],
          theme: ThemeData(colorSchemeSeed: seed, useMaterial3: true, brightness: Brightness.light),
          darkTheme: ThemeData(colorSchemeSeed: seed, useMaterial3: true, brightness: Brightness.dark),
          home: !c.onboarded
              ? const OnboardingScreen()
              : (c.pairedId == null ? const PairScreen(first: true) : const HomeShell()),
        ),
      ),
    );
  }
}

class HomeShell extends StatefulWidget {
  const HomeShell({super.key});
  @override
  State<HomeShell> createState() => _HomeShellState();
}

class _HomeShellState extends State<HomeShell> {
  int _tab = 0;
  @override
  Widget build(BuildContext context) {
    final l = L.of(context);
    const pages = [HomeScreen(), HistoryScreen(), SettingsScreen()];
    return Scaffold(
      body: SafeArea(child: pages[_tab]),
      bottomNavigationBar: NavigationBar(
        selectedIndex: _tab,
        onDestinationSelected: (i) => setState(() => _tab = i),
        destinations: [
          NavigationDestination(icon: const Icon(Icons.favorite_outline), selectedIcon: const Icon(Icons.favorite), label: l.t('home')),
          NavigationDestination(icon: const Icon(Icons.history), label: l.t('history')),
          NavigationDestination(icon: const Icon(Icons.settings_outlined), selectedIcon: const Icon(Icons.settings), label: l.t('settings')),
        ],
      ),
    );
  }
}
