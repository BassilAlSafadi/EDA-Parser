import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from pathlib import Path
import subprocess
import tempfile
import os
import re

import matplotlib
matplotlib.use("TkAgg")
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from matplotlib.figure import Figure


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

        wave_tab.rowconfigure(1, weight=1)
        wave_tab.columnconfigure(0, weight=1)

        # Toggle between the table view and the matplotlib waveform drawing.
        self.wave_view_mode = tk.StringVar(value="table")
        view_toggle = ttk.Frame(wave_tab)
        view_toggle.grid(row=0, column=0, sticky="w", pady=(0, 6))
        ttk.Label(view_toggle, text="View:").pack(side="left", padx=(0, 8))
        ttk.Radiobutton(
            view_toggle, text="Table", value="table",
            variable=self.wave_view_mode, command=self.on_wave_view_change
        ).pack(side="left")
        ttk.Radiobutton(
            view_toggle, text="Waveform Drawing", value="plot",
            variable=self.wave_view_mode, command=self.on_wave_view_change
        ).pack(side="left", padx=(8, 0))

        self.wave_view_container = ttk.Frame(wave_tab)
        self.wave_view_container.grid(row=1, column=0, sticky="nsew")
        self.wave_view_container.rowconfigure(0, weight=1)
        self.wave_view_container.columnconfigure(0, weight=1)

        # -- Table sub-view --------------------------------------------------
        self.wave_table_frame = ttk.Frame(self.wave_view_container)
        self.wave_table_frame.rowconfigure(0, weight=1)
        self.wave_table_frame.columnconfigure(0, weight=1)

        self.wave_tree = ttk.Treeview(self.wave_table_frame, show="headings")
        wave_yscroll = ttk.Scrollbar(
            self.wave_table_frame, orient="vertical", command=self.wave_tree.yview
        )
        wave_xscroll = ttk.Scrollbar(
            self.wave_table_frame, orient="horizontal", command=self.wave_tree.xview
        )
        self.wave_tree.configure(
            yscrollcommand=wave_yscroll.set,
            xscrollcommand=wave_xscroll.set
        )
        self.wave_tree.grid(row=0, column=0, sticky="nsew")
        wave_yscroll.grid(row=0, column=1, sticky="ns")
        wave_xscroll.grid(row=1, column=0, sticky="ew")

        self.wave_tree.tag_configure("even", background="#f2f2f2")
        self.wave_tree.tag_configure("odd", background="#ffffff")

        # -- Waveform drawing sub-view (matplotlib) ---------------------------
        self.wave_plot_frame = ttk.Frame(self.wave_view_container)
        self.wave_plot_frame.rowconfigure(0, weight=1)
        self.wave_plot_frame.columnconfigure(0, weight=1)

        self.wave_figure = Figure(figsize=(6, 4), dpi=100)
        self.wave_canvas = FigureCanvasTkAgg(self.wave_figure, master=self.wave_plot_frame)
        self.wave_canvas.get_tk_widget().grid(row=0, column=0, sticky="nsew")

        # Data cache used to (re)draw the current view without re-parsing.
        self._wave_rows = []
        self._wave_columns = []

        self.wave_table_frame.grid(row=0, column=0, sticky="nsew")

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
        """Parse vsim's '+sim' output ("time=N  a=0 b=1 sum=1") into rows,
        cache them, and render both the table and the plot sub-views so
        each cycle's actual signal values are visible instead of just a
        final pass/fail line."""
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

        self._wave_rows = rows
        self._wave_columns = column_order

        self.render_wave_table(rows, column_order)
        self.render_wave_plot(rows, column_order)

    def render_wave_table(self, rows, column_order):
        self.wave_tree.delete(*self.wave_tree.get_children())

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

    def render_wave_plot(self, rows, column_order):
        """Draw a digital-timing-diagram style step plot, one row of axes
        per signal, using matplotlib."""
        self.wave_figure.clear()

        if not rows or not column_order:
            ax = self.wave_figure.add_subplot(111)
            ax.text(
                0.5, 0.5,
                "Run a simulation to see the waveform drawing here.",
                ha="center", va="center", transform=ax.transAxes
            )
            ax.set_axis_off()
            self.wave_canvas.draw()
            return

        def to_float(t):
            try:
                return float(t)
            except (TypeError, ValueError):
                return None

        times = [to_float(r.get("time")) for r in rows]
        if any(t is None for t in times):
            times = list(range(len(rows)))

        n = len(column_order)
        axes = self.wave_figure.subplots(n, 1, sharex=True, squeeze=False)

        for i, name in enumerate(column_order):
            ax = axes[i][0]
            raw_values = [r.get(name, "x").lower() for r in rows]
            values = [1 if v == "1" else 0 for v in raw_values]
            # Repeat the last value/time at a trailing point so the final
            # level is visible as a held step rather than stopping mid-air.
            step_times = times + [times[-1] + 1]
            step_values = values + [values[-1]]
            ax.step(step_times, step_values, where="post", linewidth=1.5, color="tab:blue")

            # Shade unknown/high-Z cycles ("x"/"z") since they don't have a
            # real 0/1 level -- collapsing them to 0 would be misleading.
            for j, v in enumerate(raw_values):
                if v in ("x", "z"):
                    ax.axvspan(
                        step_times[j], step_times[j + 1],
                        color="0.6", alpha=0.4, linewidth=0
                    )

            ax.set_ylim(-0.3, 1.3)
            ax.set_yticks([0, 1])
            ax.set_ylabel(name, rotation=0, ha="right", va="center")
            ax.grid(True, axis="x", linestyle=":", alpha=0.5)

        axes[-1][0].set_xlabel("time")
        self.wave_figure.tight_layout()
        self.wave_canvas.draw()

    def on_wave_view_change(self):
        mode = self.wave_view_mode.get()
        self.wave_table_frame.grid_remove()
        self.wave_plot_frame.grid_remove()
        if mode == "table":
            self.wave_table_frame.grid(row=0, column=0, sticky="nsew")
        else:
            self.wave_plot_frame.grid(row=0, column=0, sticky="nsew")
            self.render_wave_plot(self._wave_rows, self._wave_columns)

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
