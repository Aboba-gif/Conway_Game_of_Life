# Conway's Game of Life

```PowerShell
# Дефолт: 512×512 grid, окно 1280×960, 30 tps
.\bin\life_app.exe

# Большая сетка
.\bin\life_app.exe --grid_w=2048 --grid_h=2048

# Non-square grid под 16:9 монитор
.\bin\life_app.exe --grid_w=960 --grid_h=540 --win_w=1920 --win_h=1080

# Пустое поле, стартует в паузе — рисовать мышью
.\bin\life_app.exe --seed=0 --paused

# Разрежённая популяция, медленная анимация
.\bin\life_app.exe --p=0.8 --ticks_per_sec=5.0

# Плотная популяция, быстрая анимация
.\bin\life_app.exe --p=0.5 --ticks_per_sec=120.0

# Воспроизводимый запуск (фиксированный seed)
.\bin\life_app.exe --seed=42 --p=0.3
```