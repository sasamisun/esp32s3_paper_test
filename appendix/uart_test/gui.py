"""
ESP32 UART File Transfer Test Tool - GUI Interface

グラフィカルユーザーインターフェース実装
"""

import os
import sys
import time
import threading
import logging
import tkinter as tk
from tkinter import ttk, filedialog, messagebox, scrolledtext
import serial.tools.list_ports
from uart_client import UARTClient, RESPONSE_NAMES

logger = logging.getLogger('UARTToolGUI')

class UARTToolGUI:
    """ESP32 UART File Transfer Test Tool のGUIインターフェース"""
    
    def __init__(self, port=None, baudrate=115200, debug=False):
        self.port = port
        self.baudrate = baudrate
        self.debug = debug
        
        self.client = UARTClient()
        self.client.set_debug(debug)
        
        self.root = None
        self.status_var = None
        self.progress_var = None
        self.connection_frame = None
        self.port_combo = None
        self.path_var = None
        self.file_tree = None
        self.log_text = None
        self.connect_button = None
        
        self.operation_lock = threading.Lock()
        self.is_busy = False
    
    def run(self):
        """GUIを表示して実行する"""
        self.root = tk.Tk()
        self.root.title("ESP32 UART File Transfer Test Tool")
        self.root.geometry("900x600")
        self.root.protocol("WM_DELETE_WINDOW", self.on_close)
        
        self.create_widgets()
        self.add_logging_handler()
        
        # 初期接続（ポートが指定されている場合）
        if self.port:
            self.root.after(500, lambda: self.connect(self.port, self.baudrate))
        
        self.root.mainloop()
    
    def create_widgets(self):
        """GUIウィジェットを作成"""
        # メインフレーム
        main_frame = ttk.Frame(self.root, padding=10)
        main_frame.pack(fill=tk.BOTH, expand=True)
        
        # ------------------------------
        # 接続設定フレーム
        # ------------------------------
        self.connection_frame = ttk.LabelFrame(main_frame, text="接続設定", padding=5)
        self.connection_frame.pack(fill=tk.X, pady=5)
        
        # 接続設定の内部コンポーネント
        ttk.Label(self.connection_frame, text="ポート:").grid(row=0, column=0, padx=5, pady=2, sticky=tk.W)
        
        self.port_combo = ttk.Combobox(self.connection_frame, width=20)
        self.port_combo.grid(row=0, column=1, padx=5, pady=2)
        self.refresh_ports()  # ポート一覧を取得
        
        ttk.Button(self.connection_frame, text="更新", command=self.refresh_ports).grid(row=0, column=2, padx=5, pady=2)
        
        ttk.Label(self.connection_frame, text="ボーレート:").grid(row=0, column=3, padx=5, pady=2, sticky=tk.W)
        
        baudrate_combo = ttk.Combobox(self.connection_frame, width=10, values=["9600", "19200", "38400", "57600", "115200", "230400", "460800"])
        baudrate_combo.grid(row=0, column=4, padx=5, pady=2)
        baudrate_combo.set(str(self.baudrate))
        
        self.connect_button = ttk.Button(self.connection_frame, text="接続", command=lambda: self.connect(self.port_combo.get(), int(baudrate_combo.get())))
        self.connect_button.grid(row=0, column=5, padx=5, pady=2)
        
        # ------------------------------
        # タブコントロール
        # ------------------------------
        tab_control = ttk.Notebook(main_frame)
        
        # タブ1: ファイルブラウザ
        file_tab = ttk.Frame(tab_control, padding=5)
        tab_control.add(file_tab, text="ファイルブラウザ")
        
        # タブ2: デバイス情報
        device_tab = ttk.Frame(tab_control, padding=5)
        tab_control.add(device_tab, text="デバイス情報")
        
        # タブ3: ログ
        log_tab = ttk.Frame(tab_control, padding=5)
        tab_control.add(log_tab, text="ログ")
        
        tab_control.pack(fill=tk.BOTH, expand=True, pady=5)
        
        # ------------------------------
        # ファイルブラウザタブの内容
        # ------------------------------
        # パス指定部分
        path_frame = ttk.Frame(file_tab)
        path_frame.pack(fill=tk.X, pady=2)
        
        ttk.Label(path_frame, text="パス:").pack(side=tk.LEFT, padx=5)
        
        self.path_var = tk.StringVar(value="/")
        path_entry = ttk.Entry(path_frame, textvariable=self.path_var, width=50)
        path_entry.pack(side=tk.LEFT, padx=5, fill=tk.X, expand=True)
        path_entry.bind("<Return>", lambda e: self.refresh_file_list())
        
        ttk.Button(path_frame, text="更新", command=self.refresh_file_list).pack(side=tk.LEFT, padx=5)
        
        # ファイルリスト
        file_list_frame = ttk.Frame(file_tab)
        file_list_frame.pack(fill=tk.BOTH, expand=True, pady=5)
        
        # ツリービュー
        self.file_tree = ttk.Treeview(file_list_frame, columns=("size", "type", "modified"), selectmode="browse")
        self.file_tree.heading("#0", text="名前")
        self.file_tree.heading("size", text="サイズ")
        self.file_tree.heading("type", text="タイプ")
        self.file_tree.heading("modified", text="更新日時")
        
        self.file_tree.column("#0", width=300)
        self.file_tree.column("size", width=100)
        self.file_tree.column("type", width=80)
        self.file_tree.column("modified", width=150)
        
        # ダブルクリックでディレクトリ移動
        self.file_tree.bind("<Double-1>", self.on_tree_double_click)
        
        # コンテキストメニュー
        self.create_context_menu()
        
        # スクロールバー
        file_scroll = ttk.Scrollbar(file_list_frame, orient="vertical", command=self.file_tree.yview)
        self.file_tree.configure(yscrollcommand=file_scroll.set)
        
        file_scroll.pack(side=tk.RIGHT, fill=tk.Y)
        self.file_tree.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        
        # ボタンフレーム
        button_frame = ttk.Frame(file_tab)
        button_frame.pack(fill=tk.X, pady=5)
        
        ttk.Button(button_frame, text="アップロード", command=self.upload_file).pack(side=tk.LEFT, padx=5)
        ttk.Button(button_frame, text="ダウンロード", command=self.download_file).pack(side=tk.LEFT, padx=5)
        ttk.Button(button_frame, text="削除", command=self.delete_selected).pack(side=tk.LEFT, padx=5)
        ttk.Button(button_frame, text="新規フォルダ", command=self.create_directory).pack(side=tk.LEFT, padx=5)
        ttk.Button(button_frame, text="フォルダ削除", command=self.delete_directory).pack(side=tk.LEFT, padx=5)
        ttk.Button(button_frame, text="情報", command=self.show_file_info).pack(side=tk.LEFT, padx=5)
        
        # ------------------------------
        # デバイス情報タブの内容
        # ------------------------------
        device_info_frame = ttk.Frame(device_tab)
        device_info_frame.pack(fill=tk.BOTH, expand=True, pady=5)
        
        # デバイス情報テキスト
        self.device_info_text = scrolledtext.ScrolledText(device_info_frame, wrap=tk.WORD, height=10)
        self.device_info_text.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        self.device_info_text.config(state=tk.DISABLED)
        
        # 操作ボタン
        device_button_frame = ttk.Frame(device_tab)
        device_button_frame.pack(fill=tk.X, pady=5)
        
        ttk.Button(device_button_frame, text="状態確認 (PING)", command=self.ping_device).pack(side=tk.LEFT, padx=5)
        ttk.Button(device_button_frame, text="デバイスリセット", command=self.reset_device).pack(side=tk.LEFT, padx=5)
        
        # ------------------------------
        # ログタブの内容
        # ------------------------------
        log_frame = ttk.Frame(log_tab)
        log_frame.pack(fill=tk.BOTH, expand=True, pady=5)
        
        # ログテキスト
        self.log_text = scrolledtext.ScrolledText(log_frame, wrap=tk.WORD)
        self.log_text.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        self.log_text.config(state=tk.DISABLED)
        
        # ログコントロール
        log_control_frame = ttk.Frame(log_tab)
        log_control_frame.pack(fill=tk.X, pady=5)
        
        ttk.Button(log_control_frame, text="クリア", command=self.clear_log).pack(side=tk.LEFT, padx=5)
        
        # デバッグモード
        debug_var = tk.BooleanVar(value=self.debug)
        debug_check = ttk.Checkbutton(log_control_frame, text="デバッグモード", variable=debug_var,
                                     command=lambda: self.set_debug_mode(debug_var.get()))
        debug_check.pack(side=tk.LEFT, padx=20)
        
        # ------------------------------
        # ステータスバー
        # ------------------------------
        status_frame = ttk.Frame(main_frame)
        status_frame.pack(fill=tk.X, pady=5)
        
        # ステータステキスト
        self.status_var = tk.StringVar(value="準備完了")
        status_label = ttk.Label(status_frame, textvariable=self.status_var, anchor=tk.W)
        status_label.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=5)
        
        # プログレスバー
        self.progress_var = tk.DoubleVar(value=0)
        progress_bar = ttk.Progressbar(status_frame, variable=self.progress_var, maximum=100, length=200)
        progress_bar.pack(side=tk.RIGHT, padx=5)
    
    def create_context_menu(self):
        """コンテキストメニューを作成"""
        self.context_menu = tk.Menu(self.root, tearoff=0)
        self.context_menu.add_command(label="情報", command=self.show_file_info)
        self.context_menu.add_separator()
        self.context_menu.add_command(label="ダウンロード", command=self.download_file)
        self.context_menu.add_command(label="アップロード", command=self.upload_file)
        self.context_menu.add_separator()
        self.context_menu.add_command(label="削除", command=self.delete_selected)
        self.context_menu.add_command(label="新規フォルダ", command=self.create_directory)
        
        self.file_tree.bind("<Button-3>", self.on_right_click)
    
    def on_right_click(self, event):
        """右クリックイベント処理"""
        if not self.client.is_connected:
            return
        
        # 選択項目の更新
        iid = self.file_tree.identify_row(event.y)
        if iid:
            self.file_tree.selection_set(iid)
            self.context_menu.post(event.x_root, event.y_root)
    
    def refresh_ports(self):
        """シリアルポート一覧を更新"""
        ports = [port.device for port in serial.tools.list_ports.comports()]
        self.port_combo['values'] = ports
        
        if ports and not self.port_combo.get():
            self.port_combo.set(ports[0])

    def connect(self, port, baudrate):
        """デバイスに接続"""
        if not port:
            messagebox.showerror("エラー", "ポートを選択してください")
            return
        
        try:
            self.set_busy(True, "接続中...")
            
            if self.client.is_connected:
                self.client.disconnect()
                self.connect_button.config(text="接続")
                self.set_status("切断しました")
                return
            
            if self.client.connect(port, baudrate):
                self.connect_button.config(text="切断")
                self.set_status(f"接続しました: {port} ({baudrate} bps)")
                self.refresh_file_list()
            else:
                messagebox.showerror("接続エラー", f"ポート {port} に接続できませんでした")
                self.set_status("接続に失敗しました")
        finally:
            self.set_busy(False)
    
    def refresh_file_list(self):
        """ファイル一覧を更新"""
        if not self.client.is_connected:
            messagebox.showinfo("情報", "デバイスに接続されていません")
            return
        
        path = self.path_var.get()
        
        try:
            self.set_busy(True, f"ファイル一覧取得中: {path}")
            
            # ツリービューをクリア
            for item in self.file_tree.get_children():
                self.file_tree.delete(item)
            
            # ファイル一覧取得
            files = self.client.get_file_list(path)
            
            if files is not None:
                # 親ディレクトリへの特殊項目
                if path != "/":
                    self.file_tree.insert("", "end", text="..", values=("", "directory", ""),
                                        image=self.get_file_icon("directory"))
                
                # ディレクトリとファイルの分離
                dirs = [f for f in files if f['type'] == 'directory']
                regular_files = [f for f in files if f['type'] == 'file']
                
                # ディレクトリ一覧
                for d in sorted(dirs, key=lambda x: x['name']):
                    mod_time = time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(d['modified']))
                    self.file_tree.insert("", "end", text=d['name'], values=("", "directory", mod_time),
                                        image=self.get_file_icon("directory"))
                
                # ファイル一覧
                for f in sorted(regular_files, key=lambda x: x['name']):
                    size_str = f"{f['size']:,} B"
                    kb_size = f['size'] / 1024
                    if kb_size >= 1:
                        size_str = f"{kb_size:.1f} KB"
                    if kb_size >= 1024:
                        mb_size = kb_size / 1024
                        size_str = f"{mb_size:.1f} MB"
                    
                    mod_time = time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(f['modified']))
                    self.file_tree.insert("", "end", text=f['name'], values=(size_str, "file", mod_time),
                                        image=self.get_file_icon(f['name']))
                
                self.set_status(f"ファイル一覧: {len(dirs)}個のディレクトリ, {len(regular_files)}個のファイル")
            else:
                self.set_status("ファイル一覧取得に失敗しました")
        finally:
            self.set_busy(False)
    
    def get_file_icon(self, name_or_type):
        """ファイルアイコンを取得（未実装）"""
        # 実際にはttk.Styleでアイコンを設定するか、PhotoImageを使用
        return None
    
    def on_tree_double_click(self, event):
        """ツリーのダブルクリックイベント処理"""
        if not self.client.is_connected:
            return
        
        selected_item = self.file_tree.selection()
        if not selected_item:
            return
        
        item_text = self.file_tree.item(selected_item, "text")
        item_type = self.file_tree.item(selected_item, "values")[1]
        
        current_path = self.path_var.get()
        
        if item_type == "directory":
            # 親ディレクトリへの移動
            if item_text == "..":
                # パスの最後のスラッシュを削除（必要なら）
                if current_path.endswith("/") and len(current_path) > 1:
                    current_path = current_path[:-1]
                
                # 最後のセグメントを削除
                new_path = current_path.rsplit("/", 1)[0]
                if not new_path:
                    new_path = "/"
            else:
                # サブディレクトリへの移動
                if current_path.endswith("/"):
                    new_path = current_path + item_text
                else:
                    new_path = current_path + "/" + item_text
            
            self.path_var.set(new_path)
            self.refresh_file_list()
        else:
            # ファイルの場合は情報表示
            self.show_file_info()
    
    def upload_file(self):
        """ファイルアップロード"""
        if not self.client.is_connected:
            messagebox.showinfo("情報", "デバイスに接続されていません")
            return
        
        current_path = self.path_var.get()
        
        # ローカルファイルを選択
        local_path = filedialog.askopenfilename(title="アップロードするファイルを選択")
        if not local_path:
            return
        
        # リモートパスの構築（ファイル名を使用）
        filename = os.path.basename(local_path)
        remote_path = current_path
        if not remote_path.endswith("/"):
            remote_path += "/"
        remote_path += filename
        
        # 確認
        if not messagebox.askyesno("確認", f"ファイルをアップロードしますか？\n\n{local_path} → {remote_path}"):
            return
        
        try:
            self.set_busy(True, f"アップロード中: {filename}")
            
            file_size = os.path.getsize(local_path)
            
            # 進捗コールバック
            def update_progress(current, total):
                if total > 0:
                    percent = (current / total) * 100
                    self.progress_var.set(percent)
                    self.set_status(f"アップロード中: {current:,}/{total:,} バイト ({percent:.1f}%)")
                else:
                    self.set_status(f"アップロード中: {current:,} バイト")
                self.root.update_idletasks()
            
            success = self.client.upload_file(local_path, remote_path, update_progress)
            
            if success:
                messagebox.showinfo("成功", f"ファイル '{filename}' のアップロードが完了しました")
                self.refresh_file_list()
            else:
                messagebox.showerror("エラー", f"ファイル '{filename}' のアップロードに失敗しました")
        finally:
            self.progress_var.set(0)
            self.set_busy(False)
    
    def download_file(self):
        """ファイルダウンロード"""
        if not self.client.is_connected:
            messagebox.showinfo("情報", "デバイスに接続されていません")
            return
        
        selected_item = self.file_tree.selection()
        if not selected_item:
            messagebox.showinfo("情報", "ダウンロードするファイルを選択してください")
            return
        
        item_text = self.file_tree.item(selected_item, "text")
        item_type = self.file_tree.item(selected_item, "values")[1]
        
        if item_type != "file":
            messagebox.showinfo("情報", "ファイルを選択してください")
            return
        
        current_path = self.path_var.get()
        
        # リモートパスの構築
        remote_path = current_path
        if not remote_path.endswith("/"):
            remote_path += "/"
        remote_path += item_text
        
        # ローカルファイルの保存先を選択
        local_path = filedialog.asksaveasfilename(title="保存先を選択", initialfile=item_text)
        if not local_path:
            return
        
        try:
            self.set_busy(True, f"ダウンロード中: {item_text}")
            
            # 進捗コールバック
            downloaded_size = 0
            
            def update_progress(current, total):
                nonlocal downloaded_size
                downloaded_size = current
                self.progress_var.set(50)  # 総サイズが不明な場合は50%表示
                self.set_status(f"ダウンロード中: {current:,} バイト")
                self.root.update_idletasks()
            
            success = self.client.download_file(remote_path, local_path, update_progress)
            
            if success:
                messagebox.showinfo("成功", f"ファイル '{item_text}' のダウンロードが完了しました\n保存先: {local_path}\nサイズ: {downloaded_size:,} バイト")
            else:
                messagebox.showerror("エラー", f"ファイル '{item_text}' のダウンロードに失敗しました")
        finally:
            self.progress_var.set(0)
            self.set_busy(False)
    
    def delete_selected(self):
        """選択したファイルを削除"""
        if not self.client.is_connected:
            messagebox.showinfo("情報", "デバイスに接続されていません")
            return
        
        selected_item = self.file_tree.selection()
        if not selected_item:
            messagebox.showinfo("情報", "削除するファイルを選択してください")
            return
        
        item_text = self.file_tree.item(selected_item, "text")
        item_type = self.file_tree.item(selected_item, "values")[1]
        
        if item_type != "file":
            messagebox.showinfo("情報", "ファイルを選択してください")
            return
        
        current_path = self.path_var.get()
        
        # パスの構築
        path = current_path
        if not path.endswith("/"):
            path += "/"
        path += item_text
        
        # 確認
        if not messagebox.askyesno("確認", f"ファイル '{path}' を削除しますか？"):
            return
        
        try:
            self.set_busy(True, f"ファイル削除中: {path}")
            
            if self.client.delete_file(path):
                messagebox.showinfo("成功", f"ファイル '{path}' を削除しました")
                self.refresh_file_list()
            else:
                messagebox.showerror("エラー", f"ファイル '{path}' の削除に失敗しました")
        finally:
            self.set_busy(False)
    
    def create_directory(self):
        """新規ディレクトリを作成"""
        if not self.client.is_connected:
            messagebox.showinfo("情報", "デバイスに接続されていません")
            return
        
        current_path = self.path_var.get()
        
        # ディレクトリ名を入力
        dir_name = tk.simpledialog.askstring("新規フォルダ", "フォルダ名を入力してください:")
        if not dir_name:
            return
        
        # 無効な文字をチェック
        if "/" in dir_name:
            messagebox.showerror("エラー", "フォルダ名に「/」は使用できません")
            return
        
        # パスの構築
        new_dir = current_path
        if not new_dir.endswith("/"):
            new_dir += "/"
        new_dir += dir_name
        
        try:
            self.set_busy(True, f"フォルダ作成中: {new_dir}")
            
            if self.client.create_directory(new_dir):
                messagebox.showinfo("成功", f"フォルダ '{new_dir}' を作成しました")
                self.refresh_file_list()
            else:
                messagebox.showerror("エラー", f"フォルダ '{new_dir}' の作成に失敗しました")
        finally:
            self.set_busy(False)
    
    def delete_directory(self):
        """ディレクトリを削除"""
        if not self.client.is_connected:
            messagebox.showinfo("情報", "デバイスに接続されていません")
            return
        
        selected_item = self.file_tree.selection()
        if not selected_item:
            messagebox.showinfo("情報", "削除するフォルダを選択してください")
            return
        
        item_text = self.file_tree.item(selected_item, "text")
        item_type = self.file_tree.item(selected_item, "values")[1]
        
        if item_type != "directory" or item_text == "..":
            messagebox.showinfo("情報", "フォルダを選択してください")
            return
        
        current_path = self.path_var.get()
        
        # パスの構築
        path = current_path
        if not path.endswith("/"):
            path += "/"
        path += item_text
        
        # 確認
        if not messagebox.askyesno("確認", f"フォルダ '{path}' を削除しますか？\n\n注意: フォルダ内のすべてのファイルも削除されます"):
            return
        
        try:
            self.set_busy(True, f"フォルダ削除中: {path}")
            
            if self.client.delete_directory(path):
                messagebox.showinfo("成功", f"フォルダ '{path}' を削除しました")
                self.refresh_file_list()
            else:
                messagebox.showerror("エラー", f"フォルダ '{path}' の削除に失敗しました")
        finally:
            self.set_busy(False)
    
    def show_file_info(self):
        """ファイル情報を表示"""
        if not self.client.is_connected:
            messagebox.showinfo("情報", "デバイスに接続されていません")
            return
        
        selected_item = self.file_tree.selection()
        if not selected_item:
            messagebox.showinfo("情報", "ファイルを選択してください")
            return
        
        item_text = self.file_tree.item(selected_item, "text")
        
        if item_text == "..":
            return
        
        current_path = self.path_var.get()
        
        # パスの構築
        path = current_path
        if not path.endswith("/"):
            path += "/"
        path += item_text
        
        try:
            self.set_busy(True, f"ファイル情報取得中: {path}")
            
            info = self.client.get_file_info(path)
            
            if info:
                info_text = f"ファイル情報: {path}\n\n"
                info_text += f"タイプ: {info['type']}\n"
                if info['type'] == 'file':
                    info_text += f"サイズ: {info['size']:,} バイト ({info['size']/1024:.1f} KB)\n"
                info_text += f"作成日時: {time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(info['created']))}\n"
                info_text += f"更新日時: {time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(info['modified']))}\n"
                
                messagebox.showinfo("ファイル情報", info_text)
            else:
                messagebox.showerror("エラー", f"ファイル情報の取得に失敗しました: {path}")
        finally:
            self.set_busy(False)
    
    def ping_device(self):
        """デバイス状態確認"""
        if not self.client.is_connected:
            messagebox.showinfo("情報", "デバイスに接続されていません")
            return
        
        try:
            self.set_busy(True, "デバイス状態確認中...")
            
            result = self.client.ping()
            
            if result:
                # デバイス情報テキストを更新
                self.update_device_info(result)
                messagebox.showinfo("デバイス状態", "デバイス状態確認成功")
            else:
                messagebox.showerror("エラー", "デバイス状態確認に失敗しました")
        finally:
            self.set_busy(False)
    
    def update_device_info(self, info):
        """デバイス情報テキストを更新"""
        self.device_info_text.config(state=tk.NORMAL)
        self.device_info_text.delete(1.0, tk.END)
        
        text = "デバイス情報:\n\n"
        text += f"ヒープメモリ空き: {info['heap_free']:,} バイト\n"
        text += f"SDカード: {'マウント済み' if info['sd_mounted'] else '未マウント'}\n"
        
        if info['sd_mounted']:
            text += f"SD総容量: {info['sd_total']:,} バイト ({info['sd_total']/1024/1024:.1f} MB)\n"
            text += f"SD空き容量: {info['sd_free']:,} バイト ({info['sd_free']/1024/1024:.1f} MB)\n"
        
        hours = info['uptime'] // 3600
        minutes = (info['uptime'] % 3600) // 60
        seconds = info['uptime'] % 60
        
        text += f"起動時間: {info['uptime']} 秒 ({hours}時間 {minutes}分 {seconds}秒)\n"
        
        self.device_info_text.insert(tk.END, text)
        self.device_info_text.config(state=tk.DISABLED)
    
    def reset_device(self):
        """デバイスリセット"""
        if not self.client.is_connected:
            messagebox.showinfo("情報", "デバイスに接続されていません")
            return
        
        if not messagebox.askyesno("確認", "デバイスをリセットしますか？\n\n注意: 接続が切断されます"):
            return
        
        try:
            self.set_busy(True, "デバイスリセット中...")
            
            if self.client.reset():
                messagebox.showinfo("成功", "デバイスリセットが実行されました")
                self.connect_button.config(text="接続")
                self.set_status("デバイスがリセットされました、接続が切断されました")
            else:
                messagebox.showerror("エラー", "デバイスリセットに失敗しました")
        finally:
            self.set_busy(False)
    
    def clear_log(self):
        """ログをクリア"""
        self.log_text.config(state=tk.NORMAL)
        self.log_text.delete(1.0, tk.END)
        self.log_text.config(state=tk.DISABLED)
    
    def set_debug_mode(self, debug):
        """デバッグモードを設定"""
        self.debug = debug
        self.client.set_debug(debug)
        
        if debug:
            logging.getLogger().setLevel(logging.DEBUG)
        else:
            logging.getLogger().setLevel(logging.INFO)
    
    def log_to_gui(self, record):
        """ログメッセージをGUIに表示"""
        if not self.log_text:
            return
        
        self.log_text.config(state=tk.NORMAL)
        
        # ログのフォーマット
        timestamp = time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(record.created))
        level = record.levelname
        message = record.getMessage()
        
        # ログレベルに応じた色を設定
        if record.levelno >= logging.ERROR:
            tag = "error"
            color = "red"
        elif record.levelno >= logging.WARNING:
            tag = "warning"
            color = "orange"
        elif record.levelno >= logging.INFO:
            tag = "info"
            color = "blue"
        else:
            tag = "debug"
            color = "gray"
        
        # タグが登録されていなければ作成
        try:
            self.log_text.tag_config(tag, foreground=color)
        except:
            pass
        
        # ログをウィンドウに追加
        self.log_text.insert(tk.END, f"{timestamp} [{level}] {message}\n", tag)
        
        # 最新部分を表示
        self.log_text.see(tk.END)
        self.log_text.config(state=tk.DISABLED)
    
    def add_logging_handler(self):
        """GUIにログを送るハンドラを追加"""
        class GUILogHandler(logging.Handler):
            def __init__(self, callback):
                super().__init__()
                self.callback = callback
            
            def emit(self, record):
                self.callback(record)
        
        # GUIハンドラを作成
        handler = GUILogHandler(self.log_to_gui)
        formatter = logging.Formatter('%(message)s')
        handler.setFormatter(formatter)
        
        # すべてのロガーにハンドラを追加
        root_logger = logging.getLogger()
        root_logger.addHandler(handler)
    
    def set_busy(self, busy, message=None):
        """ビジーステータスを設定"""
        with self.operation_lock:
            self.is_busy = busy
            
            # UIを更新
            if busy:
                self.root.config(cursor="wait")
                if message:
                    self.set_status(message)
            else:
                self.root.config(cursor="")
                self.progress_var.set(0)
                if message:
                    self.set_status(message)
                else:
                    self.set_status("準備完了")
            
            self.root.update_idletasks()
    
    def set_status(self, message):
        """ステータスメッセージを更新"""
        self.status_var.set(message)
        self.root.update_idletasks()
    
    def on_close(self):
        """アプリケーションの終了処理"""
        if self.client.is_connected:
            if messagebox.askyesno("確認", "接続を切断して終了しますか？"):
                self.client.disconnect()
                self.root.destroy()
        else:
            self.root.destroy()

# 単体テスト用
if __name__ == "__main__":
    import sys
    
    port = sys.argv[1] if len(sys.argv) > 1 else None
    app = UARTToolGUI(port)
    app.run()