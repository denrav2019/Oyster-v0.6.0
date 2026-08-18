# Oyster Programming Language

![Version](https://img.shields.io/badge/version-0.6.0-blue)
![License](https://img.shields.io/badge/license-MIT-green)

* Oyster is a minimalistic, Perl-inspired scripting programming language with a focus on simplicity and performance.
* Oyster — минималистичный скриптовый язык программирования, вдохновленный Perl и ориентированный на простоту и производительность

**Motto / Девиз:** Efficiency, readability, minimalism! / Эффективность, читаемость, минимализм!

## Features / Возможности
- Dynamic typing with 16-byte tagged values / Динамическая типизация с 16-байтными tagged values
- Perl-inspired syntax with modern enhancements / Perl-подобный синтаксис с современными улучшениями
- Fast VM with dispatch table (ASCII → handler) / Быстрая VM с таблицей диспетчеризации
- Module system with .osm (source) and .ocm (compiled) / Модульная система с .osm и .ocm с каскадной компиляцией
- Case-insensitive syntax / Регистронезависимый синтаксис
- Method-style function calls / Методные вызовы функций
- Postfix notation support / Поддержка обратной польской записи
- Fixed-point arithmetic (64.32) / Арифметика с фиксированной точкой (64.32)
- UTF-32 strings with O(1) indexed access / Строки UTF-32 с индексированным доступом O(1)

## 📦 Installation / Установка
```bash
git clone https://github.com/denrav2019/Oyster-v0.6.0.git
cd Oyster-v0.6.0
make
sudo make install
```
## Quick Start / Быстрый старт

Create a file `hello.osf`: / Создайте файл `hello.osf` :

`oyster print("Hello, Oyster!")`

Then run / Затем запустите:

```bash
./oyster hello.osf          # compile and run / компиляция и запуск
./oyster -c hello.osf       # compile only / только компиляция
./oyster -s hello.osf       # compile with source comments / с исходными комментариями
./oyster hello.oce          # run compiled bytecode / запуск скомпилированного
```

### Compiler Options / Опции компилятора
* -c Compile only, without execution / Только компиляция, без выполнения
* -s Add source lines as comments in bytecode / Добавлять исходные строки как комментарии
* -e Extended mode (float, edecimal) / Расширенный режим
* -o <file> Specify output file name / Указать имя выходного файла
* -I <path> Add path to @INC for module search / Добавить путь в @INC
* -h, --help Show help / Показать справку

## Example / Пример
```oyster
use "io" as io
use "math" as m 
@content = io.slurp("data.txt")
print("File size: " + len(@content))
$result = m.sin(&m.PI / 2)
print("sin(&m.PI/2) = " + $result)
```

# Oyster Language v0.6.0

### Быстрый старт

```bash
# Сборка
gcc -o oyster main.c compiler.c vm.c -lm -O2

# Компиляция и запуск
./oyster script.osf

# Только компиляция
./oyster -c script.osf

# Компиляция с исходными комментариями в байт-коде
./oyster -s script.osf

# Расширенный режим (float, edecimal, postfix, методы)
./oyster -e script.osf

```

## Language Reference / Справочник языка
* [Data Types / Типы данных](#Data-Types--Типы-данных)
* [Variables / Переменные](#Variables--Переменные)
* [Constants / Константы](#Constants--Константы)
* [Operators / Операторы](#Operators--Операторы)
* [Functions / Функции](#Functions--Функции)
* [Postfix Notation / Постфиксная запись выражений](#Postfix-Notation--Постфиксная-запись-выражений)
* [Modules / Модули](#Modules--Модули)
* [Script exemples / Примеры программ](#Script-exemples--Примеры-программ)
* [Stdlib](#Stdlib)

### Data Types / Типы данных
* [V_NUMBER - основной числовой тип](#V_NUMBER---основной-числовой-тип)
* [V_STRING - строка](#V_STRING---строка)
* [V_ARRAY - массив](#V_ARRAY---массив)
* [V_HASH - хеш](#V_HASH---хеш)
* [V_UNDEF - неопределённое значение](#V_UNDEF---неопределённое-значение)
* [V_FLOAT - планируется для -e режима](#V_FLOAT---планируется-для--e-режима)

#### V_NUMBER - основной числовой тип
* 64.32 fixed point. 64-bit integer part, 32-bit fractional part. Provides exact decimal fractions.
* 64.32 fixed point. Целая часть — 64 бита, дробная — 32 бита. Обеспечивает точные десятичные дроби.

```oyster
$x = 42          # nteger / целое
$y = 3.14        # with fractional part / с дробной частью
$z = 0.1 + 0.2   # 0.3 (exact! / точно!)
```

#### V_STRING - строка
* Strings in double or single quotes, based on ByteArray.
* Строки в двойных или одинарных кавычках на основе ByteArray.

```oyster
$name = "Oyster"
$path = '/usr/local/bin'

$hw = "Hello,
word!" # Multi-line strings / Многострочные строки

$hw2 = "Hellow,\n word!" # Escape sequences / Escape-последовательности (\n, \t, \\, \", \', \r)

$unistring = u"My unicode string" + u" с кириллицей" # Unicode defined string / явное задание строки Unicode

```

#### V_ARRAY - массив
* Fixed-length arrays based on ByteArray. Zero-based indexing.
* Массивы фиксированной длины на основе ByteArray. Индексация с 0.

```oyster
@arr = (10, 20, 30, 40)
$x = @arr[0]        # 10
@arr[1] = 99        # replace element / замена элемента
$len = len(@arr)    # 4

@arr2 = array(3)    # create empty array with 3 undef values / создание массива c 3 элементами undef
@arr2 = ()          # create empty array / создание пустого массива

@arr = (1 2 3 4 5)              # эквивалентно (1, 2, 3, 4, 5)
@items = ("one" "two" "three")  # эквивалентно ("one", "two", "three")
```

#### V_HASH - хеш
* Hash on linked segments. Keys are strings, values are any type.
* Хеш на связных сегментах. Ключи — строки, значения — любые типы.

```oyster
%hash = ('key1' => 100, 'key2' => "value")
$x = %hash["key1"]       # access element / доступ к элементу
%hash["key3"] = 300      # add/replace / добавление/замена
$exists = exists(%hash["key2"])  # create empty hash / проверка существования ключа

%hash = ()  # создание пустого хеша
hadd(%hash, "0.6.0", "version") 
print(%hash)
```
* Light hash — hash without keys. Created by assigning an array to a hash:
* Лёгкий хеш - хеш без ключей. Создаётся присваиванием массива хешу:
```oyster
%hash = ("one", "two", "three")
%hash = array(3)
```

#### V_UNDEF - неопределённое значение
* Undefined value. Used for uninitialized variables.
* Неопределённое значение. Используется для непроинициализированных переменных.

```oyster
$x = undef($y)           # check if undef / проверка на undef
$z = ifundef($maybe, 0)  # replace if undef / замена если undef
```

#### V_FLOAT - планируется для -e режима
IEEE 754 double. Для совместимости с аппаратной арифметикой.

```oyster
$x = 3.14f
$y = $x.sqrt()
```

### Variables / Переменные
В Oyster переменные обозначаются префиксами:

```oyster
$var — scalar / скалярная переменная (число, строка)
$x = 10

@arr — array / массив
@arr = (1, 2, 3)

%hash — hash / хеш
%h = ("a" => 1)
```

* Variables are created on first assignment:
* Переменные создаются при первом присваивании:

```oyster
$x = 100
@items = ("apple", "banana")
%config = ("debug" => 1)
```

Локальные переменные
Объявляются только внутри функций через my:

```oyster
fun calc($a, $b) {
    my $sum = $a + $b
    my $temp = $sum * 2
    return $temp
}
```

Присваивание
```oyster
$x = 42
$x = "hello"
$x = $a + $b
```

Инкремент/декремент
```oyster
inc($x)    # $x = $x + 1
dec($x)    # $x = $x - 1
```

### Constants / Константы
* Named constants are substituted at compile time:
* Именованные константы подставляются на этапе компиляции:

```oyster
&PI = 3.14159
&MAX_SIZE = 100
&GREETING = "Hello"

$circumference = 2 * &PI * $radius
```

* External module constants via prefix:
* Константы из внешних модулей доступны через префикс:

```oyster
use "math" as M
$area = &M.PI * $radius * $radius
```

### Operators / Операторы
* [Arithmetic / Арифметические](#Arithmetic--Арифметические)
* [Comparison / Сравнения](#Comparison--Сравнения)
* [Logical / Логические](#Logical--Логические)
* [Bit / Битовые](#Bit--Битовые)
* [Control Flow / Управляющие конструкции](#Control-Flow--Управляющие-конструкции)

#### Arithmetic / Арифметические
* \+    # Сложение/конкатенация строк (автоматически преобразует число в строку при конкатенации со строкой)
```oyster
$result = "Result: " + 42       # "Result: 42"
$pi = "Pi = " + 3.14            # "Pi = 3.14"
```

* \-    # Вычитание
* \*    # Умножение
* /     # Деление
* %     # Остаток от деления
* ^     # (power/степень) Возведение в степень
* \-    # (unary/унарный) Отрицание
* \++   # Инкремент
* \--   # Декремент

#### Comparison / Сравнения
* ==    # Равно
* !=    # Не равно
* \<    # Меньше
* \>    # Больше
* \<=   # Меньше или равно
* \>=   # Больше или равно

#### Logical / Логические
* and   # Логическое И
* or    # Логическое ИЛИ
* not   # Логическое НЕ

#### Bit / Битовые
* &     # Битовое И
* \|    # Битовое ИЛИ
* ^^    # Битовое XOR
* \~    # Битовое НЕ
* \<<   # Сдвиг влево
* \>>   #Сдвиг вправо

#### Control Flow / Управляющие конструкции
* [if / elseif / else](#if--elseif--else)
* [Тернарный оператор ? :](#ternary--тернарный-оператор)
* [Loops / Циклы (while / for (C-style) / for (in-style))](#loops--циклы)

#### if / elseif / else:

```oyster
if ($x > 10) {
    print($x)
} elseif ($x == 100) {
    print(100)
} elseif ($x == 300) {
    print(300)
} else {
    print(0)
}
```

#### Ternary / Тернарный оператор
```oyster
$max = ($a > $b) ? $a : $b
```

#### Loops / Циклы
* [while](#while)
* [for (C-style)](#for-c-style)
* [for (in-style)](#for-in-style)
* [last / next / redo](#loop-control-управление-циклом)

##### while:
```oyster
$i = 0
while ($i < 10) {
    print($i)
    inc($i)
}
```

##### for (C-style):
```oyster
for ($i = 0; $i < 10; $i = $i + 1) {
    print($i)
}
```

##### for (in-style):
```oyster
@array = (0 1 2 3 4 5 6 7 8 9)
for $item in @array {
    print($item)
}
```

##### Loop Control / Управление циклом
* last      # exit loop / выход из цикла
* next      # next iteration / следующая итерация
* redo      # repeat current iteration / повтор текущей итерации

Labels for nested loops / Метки для вложенных циклов:
```oyster
OUTER: while ($i < 10) {
    INNER: for $j in @arr {
        if ($j == 5) { last OUTER }
        if ($j == 3) { next INNER }
    }
}
```

### Functions / Функции
* User-defined / Пользовательские
* Built-in Functions / Встроенные функции

#### User-defined / Пользовательские

Определение функции:

```oyster
fun add($a, $b) {
    return $a + $b
}

$result = add(10, 20)
```

* Space can be used as argument separator instead of comma:
* Пробел можно использовать вместо запятой:

```oyster
$result = add(10 20)


# Export from modules / Экспорт функций из модуля:

# In module / В модуле:
fun helper($x) {
    return $x * 2
}

export helper

# In main file / В основном файле:

use "mymodule" as M
print(M.helper(5))
```

Method calls / Методные вызовы:
```oyster
$x = $a.abs()          # abs($a)
$y = "hello".length()  # length("hello")
$z = 16.0.sqrt()       # sqrt(16.0)
$fh = "file.txt".fopen("r")
$arr = @data.revers()  # revers(@data)
$x.inc()               # инкремент
$x.dec()               # декремент
```

Method chains / Цепочки методов:
```oyster
$result = $a.abs().sqrt().int()
```

#### Built-in Functions / Встроенные функции
* [Print / Печать](#Print--Печать)
* [Math / Математические](#Math--Математические)
* [String / Строковые](#String--Строковые)
* [Array/Hash/String / Для строк, массивов и хешей](#Array/Hash/String--Для-строк-массивов-и-хешей)
* [Array/String / Для строк и массивов](#Array/String--Для строк-и-массивов)
* [Array / Для массивов](#Array--Для-массивов)
* [Hash / Для хешей](#Hash--Для-хешей)
* [Undef](#Undef)
* [File / Файловые](#File--Файловые)
* [Net / Сетевые](#Net--Сетевые)
* [Serial Ports / Последовательные порты](#Serial-Ports--Последовательные-порты)
* [Processes and Pipes / Процессы и pipe](#Processes-and-Pipes--Процессы-и-pipe)

##### Print / Печать
```oyster
print("Hello, World!")
```

#### Math / Математические
Все математические функции работают с числами типа `V_NUMBER` (fixed-point 64.32).
* [abs(x)](#absx)
* [sign(x)](#signx)
* [inv(x)](#invx)
* [int(x)](#intx)
* [frac(x)](#fracx)
* [sqrt(x)](#sqrtx)
* [exp(x)](#expx)
* [ln(x)](#lnx)
* [log(x)](#logx)
* [inc(x)](#incx)
* [dec(x)](#decx)
* [round(x, n, mode)](#roundx-n-mode)

##### abs(x)
Абсолютное значение
Возвращает абсолютное значение числа (модуль).
```oyster
$r = abs(-42)      # 42
$r = abs(42)       # 42
$r = abs(0)        # 0
```

##### sign(x)
Знак числа (-1, 0, 1)
Возвращает знак числа: -1 если отрицательное, 0 если ноль, 1 если положительное.
```oyster
$r = sign(-99)     # -1
$r = sign(0)       # 0
$r = sign(42)      # 1
```

##### inv(x)
Инверсия знака. 
Меняет знак числа на противоположный (умножает на -1).
```oyster
$r = inv(5)        # -5
$r = inv(-3)       # 3
$r = inv(0)        # 0
```

##### int(x)
Целая часть числа. 
Возвращает целую часть числа, отбрасывая дробную.
```oyster
$r = int(3.14)     # 3
$r = int(-2.718)   # -2
```

##### frac(x)
Дробная часть числа. 
Возвращает дробную часть числа (знак сохраняется).
```oyster
$r = frac(3.14)    # 0.14
$r = frac(-2.5)    # -0.5
```

##### sqrt(x)
Квадратный корень. 
Вычисляет квадратный корень числа. Для отрицательных чисел возвращает undef. Использует метод Ньютона для idouble (fixed-point).
```oyster
$r = sqrt(16)      # 4
$r = sqrt(2)       # 1.4142...
$r = sqrt(-1)      # undef
```

##### exp(x)
Экспонента. 
Вычисляет экспоненту числа (e в степени x). Использует double для вычисления, результат преобразуется обратно в idouble.
```oyster
$r = exp(0)        # 1
$r = exp(1)        # 2.71828...
$r = exp(2)        # 7.38905...
```

##### ln(x)
Натуральный логарифм. 
Вычисляет натуральный логарифм числа (по основанию e). Для неположительных чисел возвращает undef.
```oyster
$r = ln(1)         # 0
$r = ln(2.71828)   # 1
$r = ln(0)         # undef
```

##### log(x)
Десятичный логарифм. 
Вычисляет десятичный логарифм числа (по основанию 10). Для неположительных чисел возвращает undef.
```oyster
$r = log(1)        # 0
$r = log(100)      # 2
$r = log(-1)       # undef
```

##### inc($x)
Инкремент переменной. 
Увеличивает значение переменной на 1. Работает только с переменными (не с выражениями). Изменяет переменную на месте.
```oyster
$x = 41
inc $x             # $x теперь 42
```

##### dec($x)
Декремент переменной. 
Уменьшает значение переменной на 1. Работает только с переменными (не с выражениями). Изменяет переменную на месте.
```oyster
$x = 43
dec $x             # $x теперь 42
```

##### round($x, $n, $mode)
Округление десятичных знаков
Округляет число $x до $n десятичных знаков.

Параметры:

$x — число (64.32 idouble)
$n — количество десятичных знаков (0–9)
$mode — режим округления:
0 — отбрасывание знаков без округления
1 — если следующий знак равен 5, округлить вверх
2 — если следующий знак равен 5, округлить вниз (отбросить)

Возвращает: округлённое число.

```oyster
# Отбрасывание (truncate)
print(round(1.09, 1, 0))   # 1.0
print(round(1.99, 0, 0))   # 1

# Округление вверх при 5
print(round(1.05, 1, 1))   # 1.1
print(round(2.55, 1, 1))   # 2.6

# Округление вниз при 5
print(round(1.05, 1, 2))   # 1.0
print(round(2.55, 1, 2))   # 2.5
```

#### String / Строковые
* [len(s)](#lens)
* [index(s, sub, pos)](#indexs-sub-pos)
* [rindex(s, sub, pos)](#rindexs-sub-pos)
* [substr(s, off, len, repl)](#substrs-off-len-repl)
* [chomp(s)](#chomps)
* [chop(s)](#chops)
* [lc(s)](#lcs)
* [uc(s)](#ucs)
* [lcfirst(s)](#lcfirsts)
* [ucfirst(s)](#ucfirsts)
* [join(sep, arr)](#joinsep-arr)
* [split(pat, s)](#splitpat-s)
* [chr(n)](#chrn)
* [ord(c)](#ordc)
* [strcmp(s1, s2)](#strcmps1-s2)
* [setstr(buf, offset, str)](#setstrbuf-offset-str)
* [getstr(buf, offset, maxlen)](#getstr-offset-maxlen)
* [writestr(fh, str)](#writestrfh-str)
* [bytearray(n)](#bytearrayn)
* [readstr(fh)](#readstrfh)
* [gets()](#gets)

##### len(s)
Длина строки. 
Возвращает длину строки в символах. Для Unicode-строк (UTF-32) возвращает количество символов, для ASCII — количество байт.
```oyster
$len = len("Hello")          # 5
$len = len(u"Привет")        # 6 (Unicode)
```

##### index(s, sub, pos)
Поиск подстроки. 
Ищет подстроку sub в строке s начиная с позиции pos (по умолчанию 0). Возвращает позицию первого вхождения или -1 если подстрока не найдена. Поддерживает Unicode.
```oyster
$pos = index("Hello World", "World")     # 6
$pos = index("Hello World", "o", 5)      # 7 (поиск с 5-й позиции)
$pos = index("Hello World", "xyz")       # -1 (не найдено)
```

##### rindex(s, sub, pos)
Поиск подстроки справа. 
Ищет подстроку sub в строке s справа налево, начиная с позиции pos (по умолчанию конец строки). Возвращает позицию последнего вхождения или -1. Поддерживает Unicode.
```oyster
$pos = rindex("Hello World World", "World")   # 12 (последнее вхождение)
$pos = rindex("Hello World", "o")             # 7
```

##### substr(s, off, len, repl)
Извлечение/замена подстроки. 
Извлекает подстроку из строки s длиной len начиная с позиции off. Если указан четвёртый аргумент repl, заменяет подстроку на repl и возвращает новую строку. Поддерживает Unicode.
```oyster
$sub = substr("Hello World", 6, 5)            # "World"
$new = substr("Hello World", 6, 5, "Oyster")  # "Hello Oyster"
```

##### chomp(s)
Убрать \n в конце. 
Удаляет завершающий символ перевода строки (\n). Если строка не заканчивается на \n, возвращает исходную строку. Поддерживает Unicode.
```oyster
$line = chomp("Hello\n")    # "Hello"
$line = chomp("Hello")      # "Hello" (без изменений)
```

##### chop(s)
Убрать последний символ. 
Удаляет последний символ строки. Для Unicode-строк удаляет один символ UTF-32. Для пустой строки возвращает пустую строку.
```oyster
$s = chop("Oyster!")       # "Oyster"
$s = chop(u"Привет!")      # u"Привет" (Unicode)
```

##### lc(s)
Нижний регистр - только ASCII. 
Преобразует все символы строки в нижний регистр. Работает только для ASCII-символов (A-Z → a-z).
```oyster
$lower = lc("HELLO")       # "hello"
```

##### uc(s)
Верхний регистр - только ASCII. 
Преобразует все символы строки в верхний регистр. Работает только для ASCII-символов (a-z → A-Z).
```oyster
$upper = uc("hello")       # "HELLO"
```

##### lcfirst(s)
Первый символ в нижний регистр - только ASCII. 
Преобразует первый символ строки в нижний регистр. Только для ASCII.
```oyster
$s = lcfirst("Hello")      # "hello"
```

##### ucfirst(s)
Первый символ в верхний регистр - только ASCII. 
Преобразует первый символ строки в верхний регистр. Только для ASCII.
```oyster
$s = ucfirst("hello")      # "Hello"
```

##### split(pat, s)
Разбить строку в массив 
Разбивает строку s по разделителю sep и возвращает массив строк. Если разделитель не найден, возвращает массив из одного элемента — исходной строки. Поддерживает Unicode.
```oyster
@parts = split(" ", "Hello World Oyster")   # ["Hello", "World", "Oyster"]
@csv = split(",", "a,b,c")                  # ["a", "b", "c"]
```

##### join(sep, arr)
Собрать массив в строку 
Собирает массив строк в одну строку, вставляя между элементами разделитель sep. Поддерживает Unicode.
```oyster
$s = join(", ", ("a", "b", "c"))   # "a, b, c"
$s = join("", ("H", "i"))          # "Hi"
```

##### chr(n)
Код символа → символ
Возвращает символ с кодом n. Для кодов 0-127 возвращает ASCII (1 байт), для кодов ≥128 возвращает Unicode (UTF-32 с маркером).
```oyster
$c = chr(65)         # "A"
$c = chr(1040)       # "А" (кириллица, Unicode)
```

##### ord(c)
Символ → код символа. 
Возвращает код первого символа строки. Для ASCII-строк возвращает байт (0-255), для Unicode-строк возвращает код UTF-32.
```oyster
$code = ord("A")          # 65
$code = ord(u"А")         # 1040 (кириллица)
```

##### strcmp(s1, s2)
Сравнивает две строки и возвращает:(-1 если s1 < s2, 0 если s1 == s2, 1 если s1 > s2) Поддерживает Unicode.
```oyster
$cmp = strcmp("abc", "abd")   # -1
$cmp = strcmp("abc", "abc")   # 0
$cmp = strcmp("abd", "abc")   # 1
```
##### setstr(buf, offset, str)
Побайтово копирует строку str в буфер buf начиная с позиции offset. Строка не должна превышать размер буфера.
```oyster
$buf = bytearray(64)
setstr($buf, 20, "Oyster")   # записать "Oyster" в буфер начиная с 20-го байта
```

##### getstr(buf, offset, maxlen)
Читает строку из буфера buf начиная с позиции offset.
Читает до maxlen байт или до нулевого байта (конец строки).
```oyster
$name = getstr($buf, 20, 44)  # прочитать до 44 байт начиная с 20-го байта
```

##### writestr(fh, str)
Записывает строку в файл в бинарном формате: сначала 4 байта длины (little-endian int32), затем сама строка. 
```oyster
writestr($fh, "Молоко 3.2%")
# В файл записано: [0E 00 00 00][Молоко 3.2%]
#                 ^-- len=14 --^  ^-- 14 байт --^
```

##### bytearray(n)
Cоздание бинарного буфера - строка ASCII длиной n, заполненная символом "\0"
Создаёт ByteArray (строку) из `n` нулевых байт. Аналог `array(n)` для массивов, но возвращает строку, которую можно использовать с функциями `set*`/`get*` для работы с бинарными данными.
В отличие от обычной строки, `bytearray` не содержит текста — это буфер фиксированной длины, заполненный нулями. Идеально подходит для формирования заголовков записей, сетевых пакетов, бинарных протоколов.

```oyster
# Создание буфера
$buf = bytearray(64)       # буфер на 64 нулевых байта
$len = len($buf)           # 64

# Запись данных в буфер
$x = set64($buf, 0, $id)         # записать 8-байтовый ID
$x = set32($buf, 16, len($name)) # записать 4-байтовую длину
setstr($buf, 20, $name)          # записать строку

# Чтение данных из буфера
$id = get64($buf, 0)             # прочитать ID
$nlen = get32($buf, 16)          # прочитать длину
$name = getstr($buf, 20, $nlen)  # прочитать строку

# Запись буфера в файл
syswrite($fh, $buf)
```

##### readstr(fh)
Читает строку из файла в бинарном формате: сначала 4 байта длины, затем сама строка. 
Возвращает прочитанную строку или пустую строку если не удалось прочитать длину.
```oyster
$str = readstr($fh)    # читает длину, потом строку
```

##### gets()
Чтение строки со стандартного ввода (читает одну строку из стандартного ввода (до \n). 
Возвращает строку без завершающего \n)
```oyster
$line = gets()
print("You entered: " + $line)
```

#### Array/Hash/String / Для строк, массивов и хешей
* [len(@arr)](#lenx)
* [clone(@x)](#conex)
* [deallocate(@x)](#deallocatex)
Эти функции работают с любым типом данных, основанным на `ByteArray`: строки (включая Unicode), массивы и хеши.

##### len(x)
Длина
Возвращает длину:
- **Строки** — количество символов (для Unicode UTF-32) или байт (для ASCII)
- **Массива** — количество элементов
- **Хеша** — количество пар ключ-значение

```oyster
$s = "Hello"
print(len($s))             # 5

$u = u"Привет"
print(len($u))             # 6 (Unicode-символов)

@arr = (1, 2, 3, 4, 5)
print(len(@arr))           # 5 (элементов)

%h = ("name" => "Oyster", "version" => "0.5.1")
print(len(%h))             # 2 (пары ключ-значение)
```
##### clone(x)
Клонировать
Создаёт полную копию строки, массива или хеша. Возвращает новый объект, независимый от исходного на верхнем уровне. Изменения в клоне не влияют на оригинал.
**Важно:** `clone()` выполняет поверхностное копирование (shallow copy). Вложенные объекты (массивы внутри массивов, хеши внутри хешей) не копируются — сохраняются ссылки на оригиналы.
```oyster
# Клонирование строки (полная копия)
$original = "Hello"
$copy = clone($original)
$copy = "World"
print($original)           # "Hello" (не изменился)

# Клонирование массива
@arr = (1, 2, 3)
@arr2 = clone(@arr)
@arr2[0] = 99
print(@arr[0])             # 1 (не изменился)

# Клонирование хеша
%h = ("key" => "value")
%h2 = clone(%h)
%h2["key"] = "new"
print(%h["key"])           # "value" (не изменился)

# Осторожно: вложенные объекты не копируются!
@inner = (1, 2)
@outer = (@inner, 3)
@outer2 = clone(@outer)
@outer2[0][0] = 99
print(@inner[0])           # 99 (изменился! это та же ссылка)
```

##### deallocate(x)
Освободить память
Освобождает память, занятую строкой, массивом или хешем. После вызова переменная становится undef. Используется для ручного управления памятью при работе с большими данными.

```oyster
@arr = array(1000000)      # большой массив
# ... используем ...
deallocate(@arr)            # освобождаем память
# @arr теперь undef

$str = "some string"
deallocate($str)            # освобождаем память

%h = (data => "value")
deallocate(%h)              # освобождаем хеш и все его сегменты
```

Важно: Oyster не имеет автоматического сборщика мусора. Используйте deallocate() для освобождения памяти, когда данные больше не нужны, особенно при работе с большими массивами, строками или хешами.

#### Array/String / Для строк и массивов
Чтение байт из ByteArray
* [get8(var, index)](#get8var-index)
* [get16(var, index)](#get16var-index)
* [get32(var, index)](#get32var-index)
* [get64(var, index)](#get64var-index)

Запись байт в ByteArray
* [set8(var, index, value)](#set8var-index-value)
* [set16(var, index, value)](#set16var-index-value)
* [set32(var, index, value)](#set32var-index-value)
* [set64(var, index, value)](#set64var-index-value)

Эти функции позволяют читать и записывать отдельные байты в строках и массивах. Работают с любым `ByteArray`: строки, массивы, хеши (через `hvalues()`/`hkeys()`).
Все значения хранятся в little-endian порядке (младший байт первым).

##### get8(var, index)
Прочитать 1 байт. 
Читает 1 байт (uint8) из переменной `var` по смещению `index`. Возвращает значение от 0 до 255.

```oyster
$buf = "Hello"
$byte = get8($buf, 0)      # 72 (код 'H')
$byte = get8($buf, 1)      # 101 (код 'e')
```

##### get16(var, index)
Прочитать 2 байта. 
Читает 2 байта (uint16, little-endian) из переменной var по смещению index. Возвращает значение от 0 до 65535.

```oyster
$buf = bytearray(64)
$x = set16($buf, 0, 0x1234)
$val = get16($buf, 0)      # 4660 (0x1234)
```

##### get32(var, index)
Прочитать 4 байта. 
Читает 4 байта (uint32, little-endian) из переменной var по смещению index. Возвращает значение от 0 до 4294967295.

```oyster
$buf = bytearray(64)
$x = set32($buf, 0, 65536)
$val = get32($buf, 0)      # 65536
```
##### get64(var, index)
Прочитать 8 байт. 
Читает 8 байт (uint64, little-endian) из переменной var по смещению index. Используется для чтения идентификаторов, смещений и других 64-битных значений.

```oyster
$buf = sysread($fh, 64)
$id = get64($buf, 0)       # прочитать ID из заголовка записи
$parent = get64($buf, 8)   # прочитать parent_id
```
##### set8(var, index, value)
Записать 1 байт. 
Записывает 1 байт (uint8) в переменную var по смещению index. Значение должно быть от 0 до 255.

```oyster
$buf = bytearray(64)
$x = set8($buf, 0, 65)     # записать 'A' (код 65)
$x = set8($buf, 16, 1)     # записать тип свойства
```
##### set16(var, index, value)
Записать 2 байта. 
Записывает 2 байта (uint16, little-endian) в переменную var по смещению index. Значение должно быть от 0 до 65535.

```oyster
$buf = bytearray(64)
$x = set16($buf, 0, 0x1234)
```
##### set32(var, index, value)
Записать 4 байта. 
Записывает 4 байта (uint32, little-endian) в переменную var по смещению index. Часто используется для записи длины строки.

```oyster
$buf = bytearray(64)
$nlen = len("Oyster")
$x = set32($buf, 16, $nlen)   # записать длину имени
```
##### set64(var, index, value)
Записать 8 байт. 
Записывает 8 байт (uint64, little-endian) в переменную var по смещению index. Используется для записи идентификаторов, смещений и других 64-битных значений.

```oyster
$buf = bytearray(64)
$x = set64($buf, 0, $id)       # записать ID
$x = set64($buf, 8, $class_id) # записать class_id
$x = set64($buf, 24, $offset)  # записать смещение в data.dat
```

Порядок байт (endianness)
Все многобайтовые значения (get16, get32, get64, set16, set32, set64) хранятся в little-endian порядке: младший байт первым.

```oyster
$buf = bytearray(4)
$x = set32($buf, 0, 0x12345678)
```
Байты в буфере: 78 56 34 12 (little-endian)
```oyster
$val = get32($buf, 0)      # 0x12345678 (305419896)
```

#### Array / Для массивов
* [revers(@arr)](#reversarr)
* [sort(@arr)](#sortarr)

##### reverse(@arr)
Перевернуть массив
Возвращает новый массив с элементами в обратном порядке. Исходный массив не изменяется.

```oyster
@arr = (1, 2, 3, 4, 5)
@rev = reverse(@arr)
print(@rev[0])    # 5
print(@rev[4])    # 1

# Исходный массив не изменился
print(@arr[0])    # 1
```

##### sort(@arr)
Отсортировать массив
Возвращает новый массив с отсортированными элементами. Исходный массив не изменяется.
Порядок сортировки:
1) undef меньше всего
2) Числа идут перед строками
3) Строки идут перед массивами
4) Числа сортируются по значению (с учётом дробной части)
5) Строки сортируются лексикографически (strcmp)
6) Массивы сортируются по длине

```oyster
@arr = (5, 2, 8, 1, 3)
@sorted = sort(@arr)
# @sorted = (1, 2, 3, 5, 8)

@mixed = (10, "apple", 5, "banana", undef, 3)
@sorted = sort(@mixed)
# @sorted = (undef, 3, 5, 10, "apple", "banana")

# Исходный массив не изменился
print(@arr[0])    # 5
```
Примечание: reverse() и sort() возвращают новый массив, не изменяя исходный. Для изменения исходного массива присвойте результат обратно: @arr = sort(@arr).

#### Hash / Для хешей
Хеши в Oyster бывают двух типов:
- **Полный хеш** — с ключами (строковыми), создаётся через `(key => value, ...)`
- **Лёгкий хеш** — без ключей, только значения, создаётся через `(val1, val2, ...)` или `array(n)`

* [exists(%hash, "key")](#existshash-key)
* [haskeys(%h)](#haskeysh)
* [setkey(%h, old_key, new_key)](#setkeyh-old_key-new_key)
* [getkey(%h, index)](#getkeyh-index)
* [hadd(%h, value, key?)](#haddh-value-key)
* [hdel(%h)](#hdelh)
* [hvalues(%h)](#hvaluesh)
* [hkeys(%h)](#hkeys)

##### exists(%hash, "key")
Проверка существования ключа. 
Проверяет, существует ли ключ `"key"` в хеше `%hash`. Возвращает `1` если ключ существует, `0` если нет. Также работает с числовыми индексами для лёгкого хеша.
```oyster
%h = ("name" => "Oyster", "version" => "0.5.1")
$has_name = exists(%h["name"])       # 1
$has_author = exists(%h["author"])   # 0

%light = ("one", "two", "three")
$has_idx = exists(%light[1])         # 1 (индекс 1 существует)
```

##### haskeys(%h)
Проверка наличия ключей. 
Проверяет, есть ли в хеше хотя бы один ключ. Возвращает 1 если ключи есть (полный хеш), 0 если ключей нет (лёгкий хеш). Используется для определения типа хеша.
```oyster
%full = (name => "Oyster")
%light = ("one", "two")

$hask = haskeys(%full)     # 1 (полный хеш)
$hask = haskeys(%light)    # 0 (лёгкий хеш)
```

##### setkey(%h, old_key, new_key)
Изменить ключ.
Изменяет ключ old_key на new_key в хеше %h. Значение, связанное с ключом, сохраняется. Если старый ключ не найден, ничего не происходит.
```oyster
%h = ("name" => "Oyster")
setkey(%h, "name", "app_name")
# Теперь: %h["app_name"] = "Oyster"
```

##### getkey(%h, index)
Получить ключ по индексу.
Возвращает ключ хеша по его порядковому индексу. Для лёгкого хеша (без ключей) возвращает undef.
```oyster
%h = ("name" => "Oyster", "version" => "0.5.1")
$key0 = getkey(%h, 0)     # "name"
$key1 = getkey(%h, 1)     # "version"
```

##### hadd(%h, value, key?)
Добавить элемент в хеш. 
Добавляет элемент в хеш. Для лёгкого хеша (без ключей) добавляет только value. Для полного хеша добавляет пару key => value. Если ключ уже существует, значение заменяется.
```oyster
# Лёгкий хеш
%light = ("one", "two")
hadd(%light, "three")         # добавляет значение

# Полный хеш
%full = (name => "Oyster")
hadd(%full, "0.5.1", "version")   # добавляет/заменяет ключ "version"
```

##### hdel(%h)
Удалить последний элемент хеша. 
Удаляет последний элемент хеша (и ключ, и значение). Уменьшает размер хеша на 1.
```oyster
%h = ("a" => 1, "b" => 2, "c" => 3)
hdel(%h)    # удаляет "c" => 3

# Теперь: %h = ("a" => 1, "b" => 2)
```

##### hvalues(%h)
Массив значений хеша - ссылка. 
Возвращает массив всех значений хеша. Возвращает ссылку на внутренний массив значений — изменения в возвращённом массиве отразятся на хеше!
```oyster
%h = ("name" => "Oyster", "version" => "0.5.1")
@vals = hvalues(%h)
print(@vals[0])    # "Oyster"
print(@vals[1])    # "0.5.1"
```

##### hkeys(%h)
Массив ключей хеша - ссылка. 
Возвращает массив всех ключей хеша. Возвращает ссылку на внутренний массив ключей — изменения в возвращённом массиве отразятся на хеше! Для лёгкого хеша возвращает массив из undef.
```oyster
%h = ("name" => "Oyster", "version" => "0.5.1")
@keys = hkeys(%h)
print(@keys[0])    # "name"
print(@keys[1])    # "version"
```
Важно: hvalues() и hkeys() возвращают ссылку на внутренние данные хеша, а не копию. Изменение возвращённого массива напрямую изменит хеш. Это сделано для производительности. Для получения копии используйте clone(@arr).

#### Undef
* [undef(x)](#undefx)
* [ifundef(x, default)](#ifundefx-default)

`undef` — специальное значение, обозначающее «не определено». Используется для непроинициализированных переменных, отсутствующих ключей хеша, ошибочных результатов.

##### undef(x)
Проверка на undef.
Проверка: 1 если x — undef
Проверяет, является ли значение `x` неопределённым. Возвращает `1` если `x` — `undef`, `0` в противном случае.

```oyster
$x = 42
print(undef($x))           # 0 (определено)

$y = undef
print(undef($y))           # 1 (не определено)

# Проверка существования переменной
if(undef($maybe)) {
    print("Variable is not set")
}

# Проверка результата функции
$fh = fopen("file.txt", "r")
if(undef($fh)) {
    print("Cannot open file")
}
```

##### ifundef(x, default)
Замена если undef.
x если не undef, иначе default.
Возвращает x если оно определено (не undef), иначе возвращает default. Удобно для установки значений по умолчанию.

```oyster
# Установка значения по умолчанию
$name = ifundef($input, "Unknown")

# Параметры функции с умолчаниями
fun greet($name) {
    $name = ifundef($name, "Guest")
    print("Hello, " + $name)
}

greet("Oyster")    # "Hello, Oyster"
greet(undef)       # "Hello, Guest"

# Безопасный доступ к хешу
$value = ifundef(%config["debug"], 0)

# Цепочка для нескольких запасных вариантов
$result = ifundef($primary, ifundef($secondary, "fallback"))
```

Отличие от литерала undef: undef без скобок создаёт неопределённое значение. undef(x) со скобками проверяет, является ли x неопределённым.

```oyster
$x = undef         # присвоить undef
$check = undef($x) # проверить, undef ли $x (вернёт 1)
```

#### File / Файловые
Файловые функции работают с файловыми дескрипторами, которые возвращаются функцией `fopen()` в виде целых чисел.

* [fopen(name, mode)](#fopenname-mode)
* [fclose(fh)](#fclosefh)
* [freadline(fh)](#freadlinefh)
* [fread(fh, len)](#freadfh-len)
* [fprint(fh, data)](#fprintfh-data)
* [fseek(fh, offset, whence)](#fseekfh-offset-whence)
* [ftell(fh)](#ftellfh)
* [feof(fh)](#feoffh)
* [sysread(fh, len)](#sysreadfh-len)
* [syswrite(fh, data)](#syswritefh-data)
* [frename(old, new)](#frenameold-new)
* [funlink(file)](#funlinkfile)

##### fopen(name, mode)
Открыть файл. 
Открывает файл `name` в режиме `mode` и возвращает файловый дескриптор (целое число). Если файл не удалось открыть, возвращает `-1`. Режимы аналогичны режимам C `fopen()`: `"r"`, `"r+"`, `"w"`, `"w+"`, `"a"`, `"a+"`, `"rb"`, `"rb+"`, `"ab"`, `"ab+"`.
```oyster
$fh = fopen("data.txt", "r")     # чтение
$fh = fopen("log.txt", "a+")     # добавление + чтение
$fh = fopen("data.bin", "rb+")   # бинарный режим
if($fh < 0) {
    print("Cannot open file")
}
```

##### fclose(fh)
Закрыть файл. 
Закрывает файл с дескриптором fh. Возвращает 1 при успехе, 0 при ошибке.
```oyster
fclose($fh)
```

##### freadline(fh)
Прочитать строку. 
Читает одну строку из файла до символа \n. Возвращает строку без завершающего \n. Если достигнут конец файла, возвращает undef.
```oyster
$line = freadline($fh)
while(!undef($line)) {
    print($line)
    $line = freadline($fh)
}
```

##### fread(fh, len)
Прочитать len байт. 
Читает len байт из файла. Возвращает строку (ByteArray) с прочитанными данными. Если прочитано меньше байт (конец файла), возвращает то что удалось прочитать.
```oyster
$data = fread($fh, 64)          # прочитать 64 байта
$header = fread($fh, 7)         # прочитать заголовок
```

##### fprint(fh, data)
Записать в файл. 
Записывает строку data в файл как текст (без добавления \n). Возвращает 1 при успехе, undef при ошибке.
```oyster
fprint($fh, "Hello World")
fprint($fh, $variable)
```

##### fseek(fh, offset, whence)
Позиционирование (whence: 0=начало, 1=текущая, 2=конец). 
Устанавливает позицию чтения/записи в файле. Аргумент whence определяет откуда считать смещение: (0 — от начала файла (SEEK_SET), 1 — от текущей позиции (SEEK_CUR), 2 — от конца файла (SEEK_END))
```oyster
fseek($fh, 0, 0)       # перейти в начало файла
fseek($fh, 0, 2)       # перейти в конец файла
fseek($fh, 128, 0)     # перейти на 128-й байт от начала
fseek($fh, -64, 1)     # вернуться на 64 байта назад
```

###### ftell(fh)
Текущая позиция. 
Возвращает текущую позицию чтения/записи в файле (в байтах от начала).
```oyster
$pos = ftell($fh)
print("Current position: " + $pos)
```

##### feof(fh)
Проверка конца файла. 
Возвращает 1 если достигнут конец файла, 0 если ещё есть данные.
```oyster
while(!feof($fh)) {
    $line = freadline($fh)
    print($line)
}
```

##### sysread(fh, len)
Системное чтение (аналогично fread). 
Аналогично fread(), но без буферизации. Читает ровно len байт (или меньше, если конец файла). Возвращает ByteArray с данными.
```oyster
$buf = sysread($fh, 64)         # прочитать 64 байта
```

##### syswrite(fh, data)
Системная запись (аналогично fprint). 
Записывает бинарные данные в файл. В отличие от fprint(), не интерпретирует строку как текст — пишет ровно те байты, которые есть в ByteArray.
```oyster
$buf = bytearray(64)
syswrite($fh, $buf)             # записать 64 нулевых байта
syswrite($fh, "Hello")          # записать строку
```

##### frename(old, new)
Переименовать файл. 
Переименовывает файл old в new. Возвращает 1 при успехе, 0 при ошибке.
```oyster
frename("old.txt", "new.txt")
```

##### funlink(file)
Удалить файл. 
Удаляет файл file. Возвращает 1 при успехе, 0 при ошибке.
```oyster
funlink("temp.txt")
```

#### Net / Сетевые
Сетевые функции работают с сокетами. Сокеты создаются через `socket()` и возвращают целочисленный дескриптор (аналогично файловым дескрипторам).
* [socket(domain, type, protocol)](#socketdomain-type-protocol)
* [connect(sock, host, port)](#connectsock-host-port)
* [bind(sock, host, port)](#bindsock-host-port)
* [listen(sock, backlog)](#listensock-backlog)
* [accept(sock)](#acceptsock)
* [send(sock, data)](#sendsock-data)
* [recv(sock, len)](#sock-len)
* [sockclose(sock)](#sockclosesock)

##### socket(domain, type, protocol)
Создать сокет. Создаёт сокет и возвращает его дескриптор (целое число). При ошибке возвращает `-1`.
Аргументы:
- `domain` — семейство адресов: `2` = AF_INET (IPv4)
- `type` — тип сокета: `1` = SOCK_STREAM (TCP), `2` = SOCK_DGRAM (UDP)
- `protocol` — протокол: `0` = автоматически

```oyster
$srv = socket(2, 1, 0)     # TCP-сокет для IPv4
```

##### connect(sock, host, port)
Подключиться к серверу. 
Подключает сокет sock к серверу host на порт port. Возвращает 1 при успехе, -1 при ошибке.
```oyster
$sock = socket(2, 1, 0)
$result = connect($sock, "192.168.1.1", 80)
if($result < 0) {
    print("Connection failed")
}
```

##### bind(sock, host, port)
Привязать сокет к адресу. 
Привязывает сокет sock к адресу host и порту port. Для привязки ко всем интерфейсам используйте "0.0.0.0". Возвращает 1 при успехе, -1 при ошибке.
```oyster
$srv = socket(2, 1, 0)
$result = bind($srv, "0.0.0.0", 9998)
```

##### listen(sock, backlog)
Слушать входящие соединения. 
Переводит сокет sock в режим прослушивания входящих соединений. backlog — максимальная очередь ожидающих соединений. Возвращает 1 при успехе, -1 при ошибке.
```oyster
listen($srv, 5)     # слушать, максимум 5 в очереди
```

##### accept(sock)
Принять входящее соединение. 
Принимает входящее соединение на сокете sock. Возвращает дескриптор нового сокета для общения с клиентом или -1 при ошибке.
```oyster
$cli = accept($srv)    # ждёт подключения клиента
```

##### send(sock, data)
Отправить данные. 
Отправляет строку data через сокет sock. Возвращает количество отправленных байт.
```oyster
$sent = send($cli, "Hello, client!")
```

##### recv(sock, len)                   
Получить данные. 
Принимает до len байт из сокета sock. Возвращает строку с полученными данными или undef при ошибке.
```oyster
$data = recv($cli, 1024)   # принять до 1024 байт
```

##### sockclose(sock)                   
Закрыть сокет. 
Закрывает сокет sock. Возвращает 1 при успехе, 0 если сокет уже закрыт или не существует.
```oyster
$result = sockclose($cli)
sockclose($srv)
```

#### Serial Ports / Последовательные порты
* [serial_open($path, $baudrate, $databits, $parity, $stopbits)](#serial_openpath-baudrate-databits-parity-stopbits)
* [serial_read($fh, $maxlen, $timeout_ms)](#serial_readfh-maxlen-timeout_ms)
* [serial_close($fh)](#serial_closefh)

##### serial_open($path, $baudrate, $databits, $parity, $stopbits)
Открывает последовательный порт и настраивает его параметры.

Параметры:

$path — путь к устройству ("/dev/ttyUSB0", "COM1")
$baudrate — скорость: 4800, 9600, 19200, 38400, 57600, 115200
$databits — биты данных: 7 или 8
$parity — чётность: 0 = нет, 1 = нечёт, 2 = чёт
$stopbits — стоп-биты: 1 или 2

Возвращает: файловый дескриптор или -1 при ошибке.

```oyster
$fh = serial_open("/dev/ttyUSB0", 115200, 8, 0, 1)
if($fh >= 0) {
    print("Порт открыт")
}
```

##### serial_read($fh, $maxlen, $timeout_ms)
Читает данные из порта с таймаутом.

Параметры:

$fh — дескриптор порта
$maxlen — максимальное число байт для чтения
$timeout_ms — таймаут в миллисекундах

Возвращает: строку с данными или пустую строку при таймауте.

```oyster
$data = serial_read($fh, 64, 200)
if($data != "") {
    print("Получено: " + $data)
}
```

##### serial_close($fh)
Закрывает последовательный порт.

Возвращает: 1 при успехе, 0 при ошибке.

```oyster
serial_close($fh)
```

#### Processes and Pipes / Процессы и pipe
* [popen2($cmd)](#popen2cmd)
* [pipe_write($idx, $data)](#pipe_writeidx-data)
* [pipe_read($idx, $maxlen, $timeout_ms)](#pipe_readidx-maxlen-timeout_ms)
* [pipe_close($idx)](#pipe_closeidx)

##### popen2($cmd)
Запускает процесс с двусторонним каналом обмена (pipe).

Параметры:

$cmd — команда для запуска ("wish", "cat", "bash")

Возвращает: индекс пары pipe или -1 при ошибке.

```oyster
$proc = popen2("wish")
if($proc >= 0) {
    print("Процесс запущен")
}
```

##### pipe_write($idx, $data)
Записывает данные в stdin запущенного процесса.
Параметры:

$idx — индекс пары pipe (из popen2)
$data — строка для отправки

Возвращает: количество записанных байт.

```oyster
pipe_write($proc, "puts Hello" + chr(10))
```

##### pipe_read($idx, $maxlen, $timeout_ms)
Читает данные из stdout процесса с таймаутом.
Параметры:

$idx — индекс пары pipe
$maxlen — максимальное число байт
$timeout_ms — таймаут в миллисекундах

Возвращает: строку с данными или пустую строку при таймауте.

```oyster
$reply = pipe_read($proc, 256, 100)
print($reply)
```

##### pipe_close($idx)
Закрывает оба канала и завершает процесс.
Параметры:

$idx — индекс пары pipe

Возвращает: 1 при успехе.

```oyster
pipe_close($proc)
```

### Postfix Notation / Постфиксная запись выражений
```oyster
$c = postfix{ $a $b + 2.0 / }
# Equivalent / Эквивалентно: $c = ($a + $b) / 2.0
```

### Modules / Модули

Подключение модуля:
```oyster
use "mymodule" as M
```

Вызов функции из модуля:
```oyster
print(M.myfunc(42))
```

Использование константы из модуля
```oyster
$x = &M.PI * 2
```

* Modules are compiled cascadingly: when compiling the main file, all dependencies are found and compiled automatically.
* Модули компилируются каскадно: при компиляции основного файла все зависимости находятся и компилируются автоматически.

### Script exemples / Примеры программ
#### Factorial / Факториал
```oyster
fun factorial($n) {
    if ($n <= 1) {
        return 1
    }
    return $n * factorial($n - 1)
}

print(factorial(10))
```

#### Sum of array elements / Сумма элементов массива
```oyster
@arr = (1, 2, 3, 4, 5)
$sum = 0
for $item in @arr {
    $sum = $sum + $item
}
print($sum)
```

#### Read file / Чтение файла
```oyster
$fh = fopen("data.txt" "r")
while (!feof($fh)) {
    $line = freadline($fh)
    print($line)
}
fclose($fh)
```

#### Echo-server / Echo-сервер
```oyster
print("Starting server on port 9998...")

$srv = socket(2, 1, 0)
$x = bind($srv, "0.0.0.0", 9998)
$x = listen($srv, 1)

print("Waiting for connection...")
$cli = accept($srv)

$data = recv($cli, 1024)
print("Received: " + $data)

$x = send($cli, $data)

sockclose($cli)
sockclose($srv)
print("Done")
```

#### Пример: HTTP-клиент
```oyster
$sock = socket(2, 1, 0)
connect($sock, "example.com", 80)

send($sock, "GET / HTTP/1.0\r\n\r\n")

while(!undef($line)) {
    $line = recv($sock, 1024)
    print($line)
}

sockclose($sock)
```
### Stdlib
Библиотека станлартных модулей. Содержит модули
* tk.osm — базовые функции Tcl/Tk
* ui.osm — элементы: окна, кнопки, таблицы, деревья
* db.osm — поддержка встроенной БД OysterDB

#### OysterDB v0.1.0
* Объектная БД с фиксированной длиной записей (64 байта)
* Классы, свойства, объекты, значения
* Иерархия классов с наследованием свойств

##### 📚 OysterDB Quick Start

```oyster
use "db" as db

# Открываем БД
%db = db.db_open("./mydb")

# Создаём классы
$root = db.db_create_class(%db, "root", 0)
$item = db.db_create_class(%db, "item", $root)

# Добавляем свойства
db.db_add_property(%db, $item, "name", &db.CLASS_STRING)
db.db_add_property(%db, $item, "price", &db.CLASS_NUMBER)

# Вставляем объект
%product = ()
%product["name"] = "Молоко 3.2%"
%product["price"] = "95.50"
$id = db.db_insert(%db, $item, %product)

# Читаем объект
%obj = db.db_select(%db, $id)
print("Name: " + %obj["name"])
print("Price: " + %obj["price"])

# Ищем объекты
@found = db.db_search(%db, $item, "name", "Молоко 3.2%")
print("Found: " + len(@found))

db.db_close(%db)
```

##### 🗂️ Структура OysterDB

* class.dat - Классы
* property.dat - Свойства
* object.dat - Объекты
* value.dat - Значения
* data.dat - Строковые данные

##### 🔧 API OysterDB
* db_open(path)                                         # Открыть/создать БД
* db_close(db)	                                        # Закрыть БД
* db_create_class(db, name, parent_id)                  # Создать класс
* db_add_property(db, class_id, name, value_class_id)   # Добавить свойство
* db_find_property(db, class_id, prop_name)             # Найти свойство по имени
* db_insert(db, class_id, data)                         # Вставить объект
* db_select(db, obj_id)                                 # Прочитать объект
* db_search(db, class_id, prop_name, value)             # Поиск объектов
* db_multysearch                                        # Поиск объектов расширенный

## Building from Source / Сборка из исходников

`make`  
`make test`  
`make install`  
`make clean`

## Dependencies / Зависимости

**Ubuntu/Debian:**  
`sudo apt install build-essential libpcre2-dev`

**Fedora/RHEL:**  
`sudo dnf install gcc make pcre2-devel`

## Author / Автор

**Daniil Kranchev**  
GitHub: [@denrav2019](https://github.com/denrav2019)  
Email: nnikus2017@gmail.com

## License / Лицензия

MIT License - see the LICENSE file for details.

## Acknowledgments / Благодарности

- Perl - or inspiration / за вдохновение
- Lua - for VM design concepts / за концепции дизайна VM
- The open source community / сообществу открытого ПО

## История релизов Oyster
### Oyster 0.6.0 - встроенные функции для работы с USB и последовательным портом RS232, графический интерфейс (базовые функции Tcl/Tk)

✅ Раздельные таблицы переменных

✅ Модуль OysterDB

✅ Serial-порты

✅ Процессы и pipe

✅ GUI (Tcl/Tk)

✅ Функция round

✅ Кэш модулей

✅ Исправления

### Oyster v0.5.1 — OysterDB
✅  OysterDB — встроенная объектная база данных
- **Фиксированная длина записей** (64 байта) — быстрый O(1) доступ по ID
- **Классы и свойства** — иерархия с наследованием
- **Типизированные свойства** — number, string, object reference
- **Бинарный формат** — `bytearray(n)`, `get*`/`set*`, `setstr`/`getstr`
- **Формат data.dat** — `[len(4 байта)][строка]`

✅  Новые функции
- `bytearray(n)` — создание бинарного буфера
- `gets()` — чтение строки со стандартного ввода
- `setstr(buf, offset, str)` / `getstr(buf, offset, maxlen)` — строки в буферах
- `writestr(fh, str)` / `readstr(fh)` — бинарный формат с длиной

✅  Улучшения
- Конкатенация строки с числом (`"Result: " + 42`)
- Пробелы как разделители в литералах массивов `(1 2 3)`
- `CallFrame.module` — внутренние функции модулей работают без экспорта
- `my` для локальных переменных в функциях

✅  Исправления
- Конфликт опкодов `V` (inv/undef) — `inv` перенесён на `o`
- `handle_concat` объединён с `handle_add`
- `set64`/`set32`/`set8` как statement с `$x =`
- `and` вместо `&&` в условиях

### Oyster v0.5.0 — Сетевые функции
✅ socket(domain, type, protocol) — создать сокет

✅ connect(sock, host, port) — подключиться

✅ bind(sock, host, port) — привязать к адресу

✅ listen(sock, backlog) — слушать

✅ accept(sock) — принять соединение

✅ send(sock, data) — отправить

✅ recv(sock, len) — получить

✅ sockclose(sock) — закрыть

### Oyster v0.4.3 — Завершение функционала файловых функций и строковых функций с поддержкой Unicode

Новое:

✅ index() — поддержка Unicode (UTF-32)

✅ rindex() — поддержка Unicode

✅ substr() — поддержка Unicode (O(1) доступ!)

✅ chop() — поддержка Unicode

✅ chomp() — поддержка Unicode

✅ Escape-последовательности в u"..."

✅ Файловые: feof, ftell, fseek, sysread, syswrite, frename, funlink


### Oyster v0.4.2 — Расширенный функционал в массиве и хеше

Новое:

✅ get8/16/32/64(var, index) — чтение байт из ByteArray

✅ set8/16/32/64(var, index, value) — запись байт в ByteArray

✅ hvalues(%h) — массив значений хеша

✅ hkeys(%h) — массив ключей хеша

✅ Добавлен , в стоп-лист для @ и %

Исправлено:

✅ get_var_index для имён с запятой

✅ Порядок аргументов в handle_get*

✅ val_number для get64
Новое:

✅ get8/16/32/64(var, index) — чтение байт из ByteArray

✅ set8/16/32/64(var, index, value) — запись байт в ByteArray

✅ hvalues(%h) — массив значений хеша

✅ hkeys(%h) — массив ключей хеша

✅ Добавлен , в стоп-лист для @ и %

Исправлено:

✅ get_var_index для имён с запятой

✅ Порядок аргументов в handle_get*

✅ val_number для get64


### Oyster v0.4.1 — поддержка Unicode

Новое:

✅ u"..." / u'...' — Unicode-строки (UTF-32 внутри)

✅ Маркер \uu\0 (4 байта) в начале Unicode-строк

✅ print() — автоматическое UTF-32 → UTF-8

✅ len() — подсчёт символов для Unicode

✅ + — конкатенация с приведением к Unicode

✅ clone() / deallocate() / chomp() / chop() — работают с Unicode

✅ Загрузка/сохранение .oce — строки с правильной длиной (без \0 обрезки)


### Oyster v0.4.0 — завершённый функционал в основном режиме

Новое:

✅ Перенос postfix и методов в обычный режим (уже работали)

✅ Методы для массивов (@arr.sort(), @arr.reverse())

✅ Пользовательские функции как методы ($x.add($y))

✅ exp(x), ln(x), log(x) для idouble

✅ clone($str) и deallocate($str) для строк

Исправлено:

✅ Исправлен стоп-лист для @ (добавлен .)

✅ Конфликт опкодов J (sqrt vs ifundef)

✅ ifundef → опкод Y

✅ handle_deallocate — убран дублирующийся V_HASH

✅ -k режим удалён из компилятора


### Changelog Oyster v0.3.6 — Управление циклами и локальные переменные
Новое:

✅ last/next/redo во всех циклах (while, for in, for C-style)

✅ Метки циклов (LOOP: while(...) { last LOOP })

✅ Индексация локальных переменных (fun можно определять в любом месте)

✅ Исправлены ошибки sqrt() и @arr[...] в print()

✅ Пробел как разделитель аргументов (везде)

Исправлено:

✅ Конфликт глобальных и локальных переменных в функциях

✅ body_start для redo в for in и for C-style

✅ Отложенный патчинг last_jumps и next_jumps для меток


### Changelog Oyster v0.3.5 — Циклы, хеши и escape-последовательности

✅ for $x in @arr — цикл по массиву
    
✅ %h["key"] в выражениях
    
✅ setkey(%h, old, new) — изменение ключа хеша
    
✅ Escape-последовательности (\n, \t, \\, \", \', \r)
    
✅ Многострочные строки
    
✅ elseif — каскадные условия
    
✅ Хеш-таблица пула строк (O(1), без дубликатов)

✅ Доступ к элементам хеша/массива по переменной-индексу (@arr[$idx])

✅ Конкатенация через +

Исправлено:

✅ hadd(%h, value) — добавление в лёгкий хеш

✅ %h = () — создание пустого хеша

✅ $ и @ с ] в стоп-листе

✅ sqrt() в print() — конфликт опкодов J


### Changelog Oyster v0.3.4 — elseif и хеш-таблица пула строк

Новое:

✅ elseif — каскадные условия (полноценный if/elseif/else)

✅ Хеш-таблица для пула строк (djb2 + открытая адресация, O(1))

✅ Устранены дубликаты строк в .oce

✅ ^ — возведение в степень

✅ + — конкатенация строк + сложение чисел (автоопределение типа)

✅ Режим -s (комментарии в байт-коде)

✅ Постфикс, методы, функции, рекурсия

Исправлено:

✅ parse_term — восстановлено тело цикла

✅ else if — не поддерживается, только elseif


### Changelog Oyster v0.3.3 — Математика и файлы

Новое:

✅ Регистронезависимый синтаксис

✅ Постфиксные выражения postfix{ ... }

✅ Методные вызовы $x.abs()

✅ Функции с параметрами и рекурсией

✅ return внутри if

✅ Базовые математические функции: abs, sign, int, frac, inv, sqrt (чистый idouble)

✅ inc $x / dec $x — инкремент/декремент переменной

✅ Конкатенация строк через + (автоопределение типа: строки → конкатенация, числа → сложение)

✅ ^ — возведение в степень (заменил **)

✅ Файловые функции: fopen, fclose, freadline, fread, fprint (вторая dispatch-таблица)

✅ Унарный минус

Исправлено:

✅ Переполнение в handle_print для дробных чисел (128-битное умножение)

✅ Конфликт опкодов I (int vs inc)

✅ handle_deallocate — дублирующийся V_HASH


### Changelog Oyster v0.3.2 — Пул строк
Новое:

✅ Поиск дубликатов в str_pool_buf (устранение повторов строк в .oce)

✅ Индекс строки = позиция в выходном буфере (не в string_pool)

Исправлено:

✅ Восстановление str_pool_pos после компиляции функций

✅ parse_term — восстановлено тело цикла (было пустым)


### Changelog Oyster v0.3.1 — Функции и строки
Новое:

✅ fun с параметрами и возвратом значений

✅ Рекурсивные функции (func_table до компиляции тела)

✅ return внутри if и общего parse_statement

✅ Строковые функции (14 отдельных опкодов): len, lc, uc, chr, ord, chomp, chop, lcfirst, ucfirst, index, rindex, substr, split, join

✅ Конкатенация строк (.)

✅ Сравнение строк (strcmp)

✅ Оператор ^ для возведения в степень

✅ Регистронезависимый синтаксис (FUN, Fun, fun)

✅ Постфиксные выражения postfix{ ... }

✅ Методные вызовы $x.abs(), $s.uc()

✅ -s режим (комментарии в байт-коде)

Исправлено:

✅ Однопроходная компиляция функций (тело в отдельный буфер)

✅ Порядок байт-кода (<E:...> перед функциями)

✅ Stack underflow после возврата из основного модуля


### Changelog v0.2.0 — Модульная система и компилятор:
Новое:

✅ Прямой формат ТДФ (<f:add:math:02:02:00000000:1>) — без индексов в пуле строк

✅ Прямой формат таблицы констант (<d:PI:0000000000000003:243F3E03>)

✅ Система модулей: use "math" as M, export

✅ Каскадная компиляция модулей

✅ Константы из внешних модулей (&Math.PI)

✅ Вызов внешних функций (Math.add(x, y))

✅ Ленивая загрузка модулей при первом вызове

✅ undef — литерал неопределённого значения

✅ undef(x) — проверка на undef

✅ ifundef(x, default) — замена если undef

✅ Базовые структуры для массивов и хешей

Исправлено:

✅ Порядок вызова get_var_index (сначала чтение имени, потом вызов)

✅ Запятая в имени переменной ($x, → $x)

✅ Пересборка ТДФ с учётом export_flag

✅ Формат умножения/деления для fixed-point

✅ Загрузка модулей в VM

✅ Парсинг .ocm файлов

✅ exists(%h, key) для хешей

