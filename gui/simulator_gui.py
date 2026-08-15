import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from pathlib import Path
import subprocess
import tempfile
import os
import re


class VerilogSimulatorGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("Verilog Simulator")
        self.root.geometry("1000x700")
        self.root.minsize(850, 600)

        # Project root = folder containing this gui/ directory.
        self.project_root = Path(__file__).resolve().parent.parent
        self.vsim_path = self.project_root / "build" / "Release" / "vsim.exe"

        self.verilog_var = tk.StringVar()
        self.vector_var = tk.StringVar()
        self.ports_var = tk.StringVar()
        self.status_var = tk.StringVar(value="Ready")

        # vsim's +sim output looks like: "time=0  a=0 b=1 sum=1 carry=0"
        self.WAVE_LINE_RE = re.compile(r"^\s*time=(\S+)(.*)$")
        self.WAVE_PAIR_RE = re.compile(r"(\S+)=([01xzXZ])(?:\s|$)")

        self.build_ui()
        self.populate_waveform("")

    def build_ui(self):
        main = ttk.Frame(self.root, padding=16)
        main.pack(fill="both", expand=True)

        title = ttk.Label(
            main,
            text="Verilog Simulator",
            font=("Segoe UI", 20, "bold")
        )
        title.pack(anchor="w", pady=(0, 16))

        files = ttk.LabelFrame(main, text="Simulation Input", padding=12)
        files.pack(fill="x", pady=(0, 12))

        ttk.Label(files, text="Verilog design (.v):").grid(
            row=0, column=0, sticky="w", padx=(0, 8), pady=6
        )
        ttk.Entry(files, textvariable=self.verilog_var).grid(
            row=0, column=1, sticky="ew", pady=6
        )
        ttk.Button(files, text="Browse...", command=self.browse_verilog).grid(
            row=0, column=2, padx=(8, 0), pady=6
        )

        ttk.Label(files, text="Vector file (.vec):").grid(
            row=1, column=0, sticky="w", padx=(0, 8), pady=6
        )
        ttk.Entry(files, textvariable=self.vector_var).grid(
            row=1, column=1, sticky="ew", pady=6
        )
        ttk.Button(files, text="Browse...", command=self.browse_vectors).grid(
            row=1, column=2, padx=(8, 0), pady=6
        )

        ttk.Label(files, text="Ports:").grid(
            row=2, column=0, sticky="w", padx=(0, 8), pady=6
        )
        ttk.Entry(files, textvariable=self.ports_var).grid(
            row=2, column=1, sticky="ew", pady=6
        )
        ttk.Label(
            files,
            text="Example: a,b,sum,carry"
        ).grid(row=2, column=2, sticky="w", padx=(8, 0), pady=6)

        files.columnconfigure(1, weight=1)

        controls = ttk.Frame(main)
        controls.pack(fill="x", pady=(0, 12))

        self.run_button = ttk.Button(
            controls,
            text="Run Simulation",
            command=self.run_simulation
        )
        self.run_button.pack(side="left")

        ttk.Button(
            controls,
            text="Clear Output",
            command=self.clear_output
        ).pack(side="left", padx=8)

        ttk.Label(
            controls,
            textvariable=self.status_var
        ).pack(side="right")

        result_frame = ttk.LabelFrame(main, text="Simulation Results", padding=8)
        result_frame.pack(fill="both", expand=True)

        self.results_notebook = ttk.Notebook(result_frame)
        self.results_notebook.pack(fill="both", expand=True)

        # --- Waveform tab: the actual per-cycle signal trace ---------------
        wave_tab = ttk.Frame(self.results_notebook, padding=(0, 6, 0, 0))
        self.results_notebook.add(wave_tab, text="Waveform")

        self.wave_tree = ttk.Treeview(wave_tab, show="headings")
        wave_yscroll = ttk.Scrollbar(
            wave_tab, orient="vertical", command=self.wave_tree.yview
        )
        wave_xscroll = ttk.Scrollbar(
            wave_tab, orient="horizontal", command=self.wave_tree.xview
        )
        self.wave_tree.configure(
            yscrollcommand=wave_yscroll.set,
            xscrollcommand=wave_xscroll.set
        )
        self.wave_tree.grid(row=0, column=0, sticky="nsew")
        wave_yscroll.grid(row=0, column=1, sticky="ns")
        wave_xscroll.grid(row=1, column=0, sticky="ew")
        wave_tab.rowconfigure(0, weight=1)
        wave_tab.columnconfigure(0, weight=1)

        self.wave_tree.tag_configure("even", background="#f2f2f2")
        self.wave_tree.tag_configure("odd", background="#ffffff")

        # --- Console log tab: raw stdout/stderr, kept for diagnostics ------
        log_tab = ttk.Frame(self.results_notebook)
        self.results_notebook.add(log_tab, text="Console Log")

        self.output = tk.Text(
            log_tab,
            wrap="none",
            font=("Consolas", 11),
            state="disabled"
        )

        yscroll = ttk.Scrollbar(
            log_tab,
            orient="vertical",
            command=self.output.yview
        )
        xscroll = ttk.Scrollbar(
            log_tab,
            orient="horizontal",
            command=self.output.xview
        )

        self.output.configure(
            yscrollcommand=yscroll.set,
            xscrollcommand=xscroll.set
        )

        self.output.grid(row=0, column=0, sticky="nsew")
        yscroll.grid(row=0, column=1, sticky="ns")
        xscroll.grid(row=1, column=0, sticky="ew")

        log_tab.rowconfigure(0, weight=1)
        log_tab.columnconfigure(0, weight=1)

        self.write_output(
            "Ready.\n"
            f"Expected simulator: {self.vsim_path}\n"
            "Select a .v file, a .vec file, enter the ports to trace, "
            "then click Run Simulation.\n"
            "The Waveform tab will show the per-cycle signal values.\n"
        )

    def browse_verilog(self):
        path = filedialog.askopenfilename(
            title="Select Verilog Design",
            filetypes=[("Verilog files", "*.v"), ("All files", "*.*")]
        )
        if path:
            self.verilog_var.set(path)

    def browse_vectors(self):
        path = filedialog.askopenfilename(
            title="Select Vector File",
            filetypes=[("Vector files", "*.vec"), ("Text files", "*.txt"),
                       ("All files", "*.*")]
        )
        if path:
            self.vector_var.set(path)

    def clear_output(self):
        self.write_output("")
        self.populate_waveform("")

    def populate_waveform(self, sim_text):
        """Parse vsim's '+sim' output ("time=N  a=0 b=1 sum=1") into rows of
        a Treeview, so each cycle's actual signal values are visible instead
        of just a final pass/fail line."""
        self.wave_tree.delete(*self.wave_tree.get_children())

        rows = []
        column_order = []
        seen = set()
        for line in sim_text.splitlines():
            m = self.WAVE_LINE_RE.match(line)
            if not m:
                continue
            row = {"time": m.group(1)}
            for name, value in self.WAVE_PAIR_RE.findall(m.group(2) + " "):
                row[name] = value
                if name not in seen:
                    seen.add(name)
                    column_order.append(name)
            rows.append(row)

        if not rows:
            self.wave_tree["columns"] = ("message",)
            self.wave_tree.heading("message", text="Waveform")
            self.wave_tree.column("message", width=650, anchor="w")
            self.wave_tree.insert(
                "", "end",
                values=("Run a simulation to see per-cycle signal values here.",)
            )
            return

        columns = ["time"] + column_order
        self.wave_tree["columns"] = columns
        self.wave_tree.column("time", width=70, anchor="center", stretch=False)
        self.wave_tree.heading("time", text="time")
        for name in column_order:
            self.wave_tree.column(name, width=90, anchor="center", stretch=False)
            self.wave_tree.heading(name, text=name)

        for i, row in enumerate(rows):
            values = [row.get(c, "") for c in columns]
            tag = "even" if i % 2 == 0 else "odd"
            self.wave_tree.insert("", "end", values=values, tags=(tag,))

    def write_output(self, text):
        self.output.configure(state="normal")
        self.output.delete("1.0", "end")
        self.output.insert("end", text)
        self.output.configure(state="disabled")

    def append_output(self, text):
        self.output.configure(state="normal")
        self.output.insert("end", text)
        self.output.see("end")
        self.output.configure(state="disabled")

    def run_simulation(self):
        verilog = Path(self.verilog_var.get().strip())
        vectors = Path(self.vector_var.get().strip())
        ports = self.ports_var.get().strip()

        if not verilog.is_file():
            messagebox.showerror("Input Error", "Please select a valid Verilog .v file.")
            return

        if not vectors.is_file():
            messagebox.showerror("Input Error", "Please select a valid .vec vector file.")
            return

        if not ports:
            messagebox.showerror(
                "Input Error",
                "Please enter the output ports/signals, e.g. a,b,sum,carry."
            )
            return

        if not self.vsim_path.is_file():
            messagebox.showerror(
                "Simulator Not Found",
                "vsim.exe was not found at:\n\n"
                f"{self.vsim_path}\n\n"
                "Build the CMake project first."
            )
            return

        # The existing project interface uses +src, +sim, +vec, +port and +simout.
        fd, output_name = tempfile.mkstemp(
            prefix="verilog_sim_",
            suffix=".out"
        )
        os.close(fd)
        output_file = Path(output_name)

        command = [
            str(self.vsim_path),
            f"+src={verilog.resolve()}",
            "+sim",
            f"+vec={vectors.resolve()}",
            f"+port={ports}",
            f"+simout={output_file.resolve()}",
        ]

        self.run_button.configure(state="disabled")
        self.status_var.set("Running...")
        self.write_output(
            "Command:\n"
            + " ".join(f'"{x}"' if " " in x else x for x in command)
            + "\n\n"
            + "Simulation output:\n"
            + "----------------------------------------\n"
        )
        self.root.update_idletasks()

        try:
            result = subprocess.run(
                command,
                cwd=self.project_root,
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace"
            )

            if result.stdout:
                self.append_output(result.stdout)

            if result.stderr:
                self.append_output(
                    "\nErrors / diagnostics:\n"
                    "----------------------------------------\n"
                    + result.stderr
                )

            simulation_file = ""
            if output_file.exists():
                simulation_file = output_file.read_text(
                    encoding="utf-8",
                    errors="replace"
                )
                if simulation_file.strip():
                    self.append_output(
                        "\nSimulation result file:\n"
                        "----------------------------------------\n"
                        + simulation_file
                    )

            self.populate_waveform(simulation_file)

            # vsim.exe currently always exits 0, even after PARSE/ELABORATE/
            # RESOLVE FAILED, so the exit code can't tell us whether the
            # simulation actually ran. Read the real outcome out of stdout
            # instead, and only trust the waveform tab when it did.
            stdout_text = result.stdout or ""
            failed_stage = next(
                (stage for stage in ("PARSE", "ELABORATE", "RESOLVE")
                 if f"{stage} FAILED" in stdout_text),
                None
            )
            simulate_ok = "SIMULATE OK" in stdout_text

            if result.returncode != 0:
                self.status_var.set(
                    f"vsim.exe crashed (exit code {result.returncode})"
                )
                self.append_output(
                    f"\nSTATUS: FAIL (exit code {result.returncode})\n"
                )
                self.results_notebook.select(1)
            elif failed_stage:
                self.status_var.set(
                    f"{failed_stage.title()} failed -- see Console Log"
                )
                self.append_output(f"\nSTATUS: {failed_stage} FAILED\n")
                self.results_notebook.select(1)
            elif not simulate_ok:
                self.status_var.set("Simulation did not run -- see Console Log")
                self.append_output("\nSTATUS: NO SIMULATION OUTPUT\n")
                self.results_notebook.select(1)
            elif not simulation_file.strip():
                self.status_var.set(
                    "Ran, but produced no signal data -- check vector "
                    "file format and port names in Console Log"
                )
                self.append_output("\nSTATUS: SIMULATE OK, NO WAVEFORM DATA\n")
                self.results_notebook.select(1)
            else:
                self.status_var.set("Simulation completed successfully")
                self.append_output("\nSTATUS: PASS / PROCESS COMPLETED\n")
                self.results_notebook.select(0)

        except Exception as exc:
            self.status_var.set("Error")
            self.append_output(f"\nGUI ERROR: {exc}\n")
            messagebox.showerror("Execution Error", str(exc))

        finally:
            try:
                output_file.unlink(missing_ok=True)
            except OSError:
                pass

            self.run_button.configure(state="normal")


if __name__ == "__main__":
    root = tk.Tk()
    app = VerilogSimulatorGUI(root)
    root.mainloop()
