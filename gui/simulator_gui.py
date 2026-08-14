import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from pathlib import Path
import subprocess
import tempfile
import os


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

        self.build_ui()

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

        self.output = tk.Text(
            result_frame,
            wrap="none",
            font=("Consolas", 11),
            state="disabled"
        )

        yscroll = ttk.Scrollbar(
            result_frame,
            orient="vertical",
            command=self.output.yview
        )
        xscroll = ttk.Scrollbar(
            result_frame,
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

        result_frame.rowconfigure(0, weight=1)
        result_frame.columnconfigure(0, weight=1)

        self.write_output(
            "Ready.\n"
            f"Expected simulator: {self.vsim_path}\n"
            "Select a .v file, a .vec file, enter the output ports, "
            "then click Run Simulation.\n"
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

            if result.returncode == 0:
                self.status_var.set("Simulation completed successfully")
                self.append_output("\nSTATUS: PASS / PROCESS COMPLETED\n")
            else:
                self.status_var.set(
                    f"Simulation failed (exit code {result.returncode})"
                )
                self.append_output(
                    f"\nSTATUS: FAIL (exit code {result.returncode})\n"
                )

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
