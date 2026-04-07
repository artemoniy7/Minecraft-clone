#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Программа для отображения структуры папок и файлов в виде дерева.
Запустите её в нужной директории – она покажет все содержимое.
"""

import os
from pathlib import Path

def print_tree(directory: Path, prefix: str = "", output_file=None) -> None:
    """
    Рекурсивно выводит содержимое директории в виде дерева.

    :param directory: Путь к директории (объект Path)
    :param prefix: Строка-префикс для форматирования отступов
    :param output_file: Файловый объект для записи (если None, печатается в консоль)
    """
    try:
        # Получаем все элементы директории, сортируем (папки выше, затем файлы)
        items = sorted(
            directory.iterdir(),
            key=lambda p: (not p.is_dir(), p.name.lower())
        )
    except PermissionError:
        # Если нет прав на чтение папки, сообщаем об ошибке
        message = f"{prefix}[Нет доступа] {directory.name}/"
        if output_file:
            output_file.write(message + "\n")
        else:
            print(message)
        return

    # Перебираем все элементы с индексом, чтобы определить последний
    for i, path in enumerate(items):
        is_last = (i == len(items) - 1)

        # Выбираем символ для отображения
        connector = "└── " if is_last else "├── "
        line = prefix + connector + path.name

        # Если это папка, добавляем слэш в конце (опционально)
        if path.is_dir():
            line += "/"

        # Вывод строки
        if output_file:
            output_file.write(line + "\n")
        else:
            print(line)

        # Если это папка, рекурсивно обходим её содержимое
        if path.is_dir():
            # Новый префикс для дочерних элементов
            extension = "    " if is_last else "│   "
            print_tree(path, prefix + extension, output_file)

def main():
    # Текущая рабочая директория (где находится скрипт или откуда запущен)
    start_dir = Path.cwd()

    # Можно также явно взять директорию скрипта:
    # start_dir = Path(__file__).parent

    print(f"Структура папки: {start_dir}\n")

    # Выводим корневой элемент "."
    print(".")
    # Рекурсивно печатаем содержимое
    print_tree(start_dir, "    ")

    # Если нужно сохранить в файл, раскомментируйте следующие строки:
    # with open("tree_output.txt", "w", encoding="utf-8") as f:
    #     f.write(f"Структура папки: {start_dir}\n\n.\n")
    #     print_tree(start_dir, "    ", output_file=f)
    # print("\nРезультат также сохранён в файл 'tree_output.txt'")

if __name__ == "__main__":
    main()