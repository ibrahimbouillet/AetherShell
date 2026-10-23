# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
"""Create private, complete-frame review evidence from an explicit local MP4.

This tool does not capture the desktop, control applications, accept a privacy review,
publish media, or measure source acquisition cadence. Console errors are categorical.
"""
from __future__ import annotations

import argparse
import bisect
import hashlib
import json
import math
import re
import shutil
import statistics
import subprocess
import sys
import uuid
from fractions import Fraction
from pathlib import Path


class ReviewError(Exception):
    pass


class PrivateParser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        raise ReviewError("arguments")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def local_path(value: str) -> Path:
    if value.startswith(("\\\\", "//")):
        raise ReviewError("nonlocal_path")
    path = Path(value).expanduser().resolve()
    if str(path).startswith(("\\\\", "//")):
        raise ReviewError("nonlocal_path")
    return path


def find_ffmpeg(explicit: str | None) -> Path:
    if explicit:
        result = local_path(explicit)
    else:
        executable = None
        try:
            import imageio_ffmpeg
            executable = imageio_ffmpeg.get_ffmpeg_exe()
        except (ImportError, RuntimeError):
            executable = shutil.which("ffmpeg")
        if not executable:
            raise ReviewError("ffmpeg_unavailable")
        result = local_path(executable)
    if not result.is_file():
        raise ReviewError("ffmpeg_unavailable")
    return result


def execute(arguments: list[str], output: Path) -> None:
    try:
        result = subprocess.run(arguments, cwd=output, stdin=subprocess.DEVNULL,
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
    except OSError as error:
        raise ReviewError("decoder_start") from error
    if result.returncode:
        raise ReviewError("decoder_failed")


def decoder_input(ffmpeg: Path, source: Path) -> list[str]:
    # A local filename is not a format/protocol policy. Reject mislabeled playlists and keep
    # both decode passes restricted to the local MP4/MOV demuxer, without network protocols.
    return [str(ffmpeg), "-hide_banner", "-loglevel", "error", "-nostdin",
            "-protocol_whitelist", "file,pipe", "-f", "mov", "-copyts", "-i", str(source)]


def parse_framehash(path: Path) -> tuple[int, int, Fraction, list[dict]]:
    size = None
    time_base = None
    frames = []
    for line in path.read_text(encoding="ascii").splitlines():
        if match := re.fullmatch(r"#dimensions 0:\s*(\d+)x(\d+)", line):
            size = int(match[1]), int(match[2])
        elif match := re.fullmatch(r"#tb 0:\s*(\d+)/(\d+)", line):
            time_base = Fraction(int(match[1]), int(match[2]))
        elif line and not line.startswith("#"):
            fields = [field.strip() for field in line.split(",")]
            if len(fields) != 6 or int(fields[0]) != 0:
                raise ReviewError("timestamp_metadata")
            frames.append({"index": len(frames), "pts": int(fields[2]), "duration": int(fields[3])})
    if not size or not time_base or not frames:
        raise ReviewError("video_stream_missing")
    if min(size) < 1 or max(size) > 16384 or size[0] * size[1] > 40_000_000:
        raise ReviewError("frame_size_limit")
    if any(frames[i]["pts"] <= frames[i - 1]["pts"] for i in range(1, len(frames))):
        raise ReviewError("nonmonotonic_timestamps")
    return size[0], size[1], time_base, frames


def add_luma(path: Path, frames: list[dict], threshold: float) -> None:
    index = None
    found = set()
    for line in path.read_text(encoding="ascii").splitlines():
        if match := re.match(r"frame:(\d+)\s", line):
            index = int(match[1])
            if index >= len(frames):
                raise ReviewError("luma_metadata")
        elif match := re.fullmatch(r"lavfi\.signalstats\.(YAVG|YMAX)=(\d+(?:\.\d+)?)", line):
            if index is None:
                raise ReviewError("luma_metadata")
            key = "mean_luma_8bit" if match[1] == "YAVG" else "maximum_luma_8bit"
            frames[index][key] = float(match[2])
            found.add((index, key))
    if len(found) != len(frames) * 2:
        raise ReviewError("luma_metadata")
    for frame in frames:
        frame["near_black_mean"] = frame["mean_luma_8bit"] <= threshold
        frame["entire_frame_near_black_luma"] = frame["maximum_luma_8bit"] <= threshold


def selected_stills(value: str, frames: list[dict], time_base: Fraction) -> set[int]:
    selected = {0, len(frames) - 1}
    times = [float((frame["pts"] - frames[0]["pts"]) * time_base) for frame in frames]
    end_time = float((max(frame["pts"] + frame["duration"] for frame in frames) - frames[0]["pts"]) * time_base)
    for item in filter(None, (item.strip() for item in value.split(","))):
        if item in ("first", "last"):
            selected.add(0 if item == "first" else len(frames) - 1)
        elif re.fullmatch(r"\d+(?:\.\d+)?s", item):
            seconds = float(item[:-1])
            if not math.isfinite(seconds) or seconds > end_time:
                raise ReviewError("still_selection")
            at = bisect.bisect_left(times, seconds)
            if at and (at == len(times) or seconds - times[at - 1] <= times[at] - seconds):
                at -= 1
            selected.add(at)
        elif item.isascii() and item.isdecimal() and int(item) < len(frames):
            selected.add(int(item))
        else:
            raise ReviewError("still_selection")
    return selected


def read_exact(stream, length: int) -> bytes:
    result = bytearray()
    while len(result) < length:
        block = stream.read(length - len(result))
        if not block:
            break
        result.extend(block)
    return bytes(result)


def make_contacts(ffmpeg: Path, source: Path, output: Path, width: int, height: int,
                  time_base: Fraction, frames: list[dict], args) -> tuple[list[dict], list[dict]]:
    try:
        from PIL import Image, ImageDraw, ImageFont
    except ImportError as error:
        raise ReviewError("pillow_unavailable") from error
    thumb_width = min(args.thumbnail_width, width)
    thumb_height = max(1, round(height * thumb_width / width))
    cell_width, cell_height = max(thumb_width, 190) + 8, thumb_height + 30
    sheet_size = args.columns * cell_width + 8, args.rows * cell_height + 8
    if sheet_size[0] * sheet_size[1] > 32_000_000:
        raise ReviewError("contact_size_limit")
    selected = selected_stills(args.stills, frames, time_base)
    count, sheets, stills = 0, [], []
    capacity = args.columns * args.rows
    font = ImageFont.load_default(size=13)
    canvas = None
    command = decoder_input(ffmpeg, source) + [
               "-map", "0:v:0", "-an", "-pix_fmt", "rgb24", "-fps_mode", "passthrough",
               "-enc_time_base", "demux", "-f", "rawvideo", "pipe:1"]
    try:
        process = subprocess.Popen(command, cwd=output, stdin=subprocess.DEVNULL,
                                   stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    except OSError as error:
        raise ReviewError("decoder_start") from error
    try:
        while pixels := read_exact(process.stdout, width * height * 3):
            if len(pixels) != width * height * 3 or count >= len(frames):
                raise ReviewError("decode_count_mismatch")
            if count % capacity == 0:
                canvas = Image.new("RGB", sheet_size, "#15191f")
            image = Image.frombytes("RGB", (width, height), pixels)
            if count in selected:
                filename = f"frame-{count:06d}.png"
                image.save(output / filename)
                stills.append({"frame": count, "file": filename})
            image.thumbnail((thumb_width, thumb_height), Image.Resampling.LANCZOS)
            slot = count % capacity
            x, y = 8 + slot % args.columns * cell_width, 8 + slot // args.columns * cell_height
            canvas.paste(image, (x, y))
            elapsed = float((frames[count]["pts"] - frames[0]["pts"]) * time_base)
            ImageDraw.Draw(canvas).text((x, y + thumb_height + 5), f"#{count:06d}  {elapsed:.6f} s", font=font, fill="white")
            count += 1
            if count % capacity == 0 or count == len(frames):
                filename = f"contacts-{len(sheets) + 1:04d}.png"
                canvas.save(output / filename)
                sheets.append({"file": filename, "first_frame": count - slot - 1, "last_frame": count - 1})
                canvas = None
        if process.wait() or count != len(frames):
            raise ReviewError("decode_count_mismatch")
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        if process.stdout:
            process.stdout.close()
    return sheets, stills


def review(args, output: Path) -> dict:
    source = local_path(args.input)
    if not source.is_file() or source.suffix.casefold() != ".mp4":
        raise ReviewError("input_mp4_missing")
    ffmpeg = find_ffmpeg(args.ffmpeg)
    source_hash = sha256(source)
    execute(decoder_input(ffmpeg, source) + [
             "-map", "0:v:0", "-an", "-vf", "format=yuv420p,signalstats,metadata=mode=print:file=signalstats.txt",
             "-fps_mode", "passthrough", "-enc_time_base", "demux", "-f", "framehash", "-hash", "SHA256",
             "framehash.txt"], output)
    width, height, time_base, frames = parse_framehash(output / "framehash.txt")
    add_luma(output / "signalstats.txt", frames, args.black_yavg)
    sheets, stills = make_contacts(ffmpeg, source, output, width, height, time_base, frames, args)
    if sha256(source) != source_hash:
        raise ReviewError("input_changed")
    steps = [frames[i]["pts"] - frames[i - 1]["pts"] for i in range(1, len(frames))]
    elapsed = float((frames[-1]["pts"] - frames[0]["pts"]) * time_base)
    duration = float((max(frame["pts"] + frame["duration"] for frame in frames) - frames[0]["pts"]) * time_base)
    return {
        "schema": 1, "status": "complete", "source_sha256": source_hash, "source_file_size_bytes": source.stat().st_size,
        "tool_sha256": sha256(Path(__file__)), "width": width, "height": height, "decoded_frames": len(frames),
        "time_base": f"{time_base.numerator}/{time_base.denominator}", "first_pts": frames[0]["pts"],
        "duration_seconds_from_frame_timestamps": duration, "strictly_increasing_pts": True,
        "uniform_pts_intervals": len(set(steps)) == 1 if steps else None,
        "presentation_fps_from_pts_span": (len(frames) - 1) / elapsed if elapsed else None,
        "median_pts_interval_seconds": float(statistics.median(steps) * time_base) if steps else None,
        "minimum_pts_interval_seconds": float(min(steps) * time_base) if steps else None,
        "maximum_pts_interval_seconds": float(max(steps) * time_base) if steps else None,
        "contact_sheets": sheets, "full_resolution_stills": stills, "contact_coverage_frames": len(frames),
        "luma_analysis": {"format": "FFmpeg yuv420p, 8-bit digital luma", "near_black_threshold": args.black_yavg,
                          "minimum_mean_luma": min(frame["mean_luma_8bit"] for frame in frames),
                          "maximum_mean_luma": max(frame["mean_luma_8bit"] for frame in frames),
                          "near_black_mean_frames": [frame["index"] for frame in frames if frame["near_black_mean"]],
                          "entire_frame_near_black_luma_frames": [frame["index"] for frame in frames if frame["entire_frame_near_black_luma"]],
                          "interpretation": "Threshold observations, not proof of blank content or privacy."},
        "frames": frames, "source_capture_cadence": "not_measured_by_this_tool",
        "audio_review": "not_performed", "human_privacy_review": "pending", "normal_speed_motion_review": "pending",
        "publication": "not_performed", "notes": ["Every decoded frame appears once in the contact sheets.",
            "Contacts are reduced-resolution evidence; inspect critical full-resolution stills and real playback separately.",
            "Optional still timestamps are seconds relative to the first decoded frame and select the nearest frame."]}


def main() -> int:
    output = None
    created_output = False
    try:
        parser = PrivateParser(description=__doc__)
        parser.add_argument("input", help="Explicit local MP4; no capture or application control.")
        parser.add_argument("--ffmpeg", help="Installed FFmpeg executable; otherwise use installed imageio_ffmpeg or PATH.")
        parser.add_argument("--output", help="New private output directory; must not already exist.")
        parser.add_argument("--stills", default="", help="Extra frame indices or relative timestamps, for example 12,2.5s,6s. First/last are always saved.")
        parser.add_argument("--columns", type=int, default=6)
        parser.add_argument("--rows", type=int, default=8)
        parser.add_argument("--thumbnail-width", type=int, default=320)
        parser.add_argument("--black-yavg", type=float, default=18.0, help="Near-black 8-bit luma threshold; default 18.")
        args = parser.parse_args()
        if not (1 <= args.columns <= 12 and 1 <= args.rows <= 24 and 64 <= args.thumbnail_width <= 1024 and
                math.isfinite(args.black_yavg) and 0 <= args.black_yavg <= 255):
            raise ReviewError("arguments")
        root = Path(__file__).resolve().parents[2]
        default_output = root / ".local" / "showcase-review" / uuid.uuid4().hex
        output = local_path(args.output or str(default_output))
        try:
            relative = output.relative_to(root)
            if not relative.parts or relative.parts[0] not in (".local", "build"):
                raise ReviewError("public_output_location")
        except ValueError:
            pass
        if output.exists():
            output = None  # Never write a failure receipt into an existing directory.
            raise ReviewError("output_exists")
        output.mkdir(parents=True, exist_ok=False)
        created_output = True
        result = review(args, output)
        (output / "review.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print(f"Review complete: {result['decoded_frames']} frames, {len(result['contact_sheets'])} contact sheets; human privacy and motion review pending.")
        return 0
    except KeyboardInterrupt:
        category = "cancelled"
    except ReviewError as error:
        category = str(error)
    except Exception:
        category = "review_failed"
    if created_output and output is not None and output.is_dir():
        try:
            (output / "failure.json").write_text(json.dumps({"status": "failed", "category": category}) + "\n", encoding="utf-8")
        except OSError:
            pass
    print("Review failed: " + category, file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
