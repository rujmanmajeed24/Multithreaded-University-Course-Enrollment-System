"""
server.py  —  Flask backend for the University Enrollment Portal
Bridges the browser (HTML/JS) and the C++ data layer (enrollments.txt / students.txt)
Run with:  python server.py
"""

from flask import Flask, request, jsonify, send_from_directory
from flask_cors import CORS
import json, os, csv

app = Flask(__name__, static_folder="portal")
CORS(app)

# ── paths ────────────────────────────────────────────────────
BASE      = os.path.dirname(os.path.abspath(__file__))
STUDENTS  = os.path.join(BASE, "students.txt")
ENROLL_F  = os.path.join(BASE, "enrollments.txt")

# ── in-memory course catalogue (mirrors C++ initCourses) ─────
COURSES = [
    {"code":"CS101","title":"Intro to Programming",    "dept":"Computer",     "seats":30},
    {"code":"CS201","title":"Data Structures",         "dept":"Computer",     "seats":25},
    {"code":"CS301","title":"Operating Systems",       "dept":"Computer",     "seats":20},
    {"code":"CS401","title":"Computer Networks",       "dept":"Computer",     "seats":20},
    {"code":"CS501","title":"Artificial Intelligence", "dept":"Computer",     "seats":15},

    {"code":"EE101","title":"Circuit Analysis",        "dept":"Electrical",   "seats":30},
    {"code":"EE201","title":"Electronics",             "dept":"Electrical",   "seats":25},
    {"code":"EE301","title":"Signals & Systems",       "dept":"Electrical",   "seats":20},
    {"code":"EE401","title":"Power Systems",           "dept":"Electrical",   "seats":20},
    {"code":"EE501","title":"Control Systems",         "dept":"Electrical",   "seats":15},

    {"code":"ME101","title":"Engineering Mechanics",   "dept":"Mechanical",   "seats":30},
    {"code":"ME201","title":"Thermodynamics",          "dept":"Mechanical",   "seats":25},
    {"code":"ME301","title":"Fluid Mechanics",         "dept":"Mechanical",   "seats":20},
    {"code":"ME401","title":"Machine Design",          "dept":"Mechanical",   "seats":20},
    {"code":"ME501","title":"Manufacturing Processes", "dept":"Mechanical",   "seats":15},

    {"code":"MT101","title":"Intro to Mechatronics",   "dept":"Mechatronics", "seats":30},
    {"code":"MT201","title":"Sensors & Actuators",     "dept":"Mechatronics", "seats":25},
    {"code":"MT301","title":"Embedded Systems",        "dept":"Mechatronics", "seats":20},
    {"code":"MT401","title":"Robotics",                "dept":"Mechatronics", "seats":20},
    {"code":"MT501","title":"Industrial Automation",   "dept":"Mechatronics", "seats":15},
]

PRIORITY_LABELS = {5:"Final Year", 4:"3rd Year", 3:"2nd Year", 2:"1st Year", 1:"Freshman"}

# ── helpers ──────────────────────────────────────────────────
def load_students():
    students = []
    if not os.path.exists(STUDENTS):
        # demo data
        return [
            {"id":1001,"name":"Alice",  "dept":"Computer",     "priority":4},
            {"id":1002,"name":"Bob",    "dept":"Computer",     "priority":2},
            {"id":1003,"name":"Charlie","dept":"Computer",     "priority":3},
            {"id":1004,"name":"Diana",  "dept":"Computer",     "priority":5},
            {"id":1005,"name":"Eve",    "dept":"Computer",     "priority":1},
            {"id":1006,"name":"Frank",  "dept":"Electrical",   "priority":3},
            {"id":1007,"name":"Grace",  "dept":"Mechanical",   "priority":4},
            {"id":1008,"name":"Hina",   "dept":"Mechatronics", "priority":2},
        ]
    with open(STUDENTS) as f:
        for line in f:
            line = line.strip()
            if not line: continue
            parts = line.split(",")
            if len(parts) >= 4:
                students.append({
                    "id":int(parts[0]),
                    "name":parts[1].strip(),
                    "dept":parts[2].strip(),
                    "priority":int(parts[3].strip())
                })
    return students

def load_enrollments():
    """Returns list of {id, name, code, dept} records."""
    records = []
    if not os.path.exists(ENROLL_F): return records
    with open(ENROLL_F) as f:
        for line in f:
            line = line.strip()
            if not line: continue
            parts = line.split(",")
            if len(parts) >= 4:
                records.append({
                    "id":int(parts[0]),
                    "name":parts[1].strip(),
                    "code":parts[2].strip(),
                    "studentDept":parts[3].strip()
                })
    return records

def enrolled_codes_for(student_id):
    return [r["code"] for r in load_enrollments() if r["id"] == student_id]

def course_enrolled_count(code):
    return sum(1 for r in load_enrollments() if r["code"] == code)

def get_course(code):
    for c in COURSES:
        if c["code"] == code: return c
    return None

def build_student_stats(student):
    sid       = student["id"]
    sdept     = student["dept"]
    records   = [r for r in load_enrollments() if r["id"] == sid]
    codes     = [r["code"] for r in records]
    dept_count = 0
    elec_count = 0
    for r in records:
        c = get_course(r["code"])
        if c:
            if c["dept"] == sdept: dept_count += 1
            else:                  elec_count += 1
    return {
        "total": len(codes),
        "deptCount": dept_count,
        "electiveCount": elec_count,
        "enrolledCodes": codes
    }

def bankers_safe(course_index, requester_id, students_list):
    """Simplified Banker's safety check (mirrors C++ logic)."""
    MAX_DEMAND = 6
    n = len(students_list)
    m = len(COURSES)

    available = []
    for c in COURSES:
        cnt = course_enrolled_count(c["code"])
        available.append(c["seats"] - cnt)

    if available[course_index] <= 0:
        return False
    available[course_index] -= 1

    # need per student
    enrollments = load_enrollments()
    def student_enrolled_count(sid):
        return sum(1 for r in enrollments if r["id"] == sid)

    need = []
    for s in students_list:
        n_val = MAX_DEMAND - student_enrolled_count(s["id"])
        if s["id"] == requester_id:
            n_val = max(0, n_val - 1)
        need.append(n_val)

    total_avail = sum(available)
    finished = [False] * n
    count = 0
    while count < n:
        found = False
        for i in range(n):
            if not finished[i] and need[i] <= total_avail:
                total_avail += student_enrolled_count(students_list[i]["id"])
                finished[i] = True
                count += 1
                found = True
        if not found:
            break
    return count == n

def simulate_priority_queue(course_index, course_code, course_dept,
                             requester_id, requester_dept, students_list):
    """
    Simulate thread priority scheduling.
    Returns list of {name, priority, threadNum, status, reason, seatsAfter}
    """
    import random, time

    # Build queue: course dept students + requester
    queue = []
    for s in students_list:
        if s["dept"] == course_dept or s["id"] == requester_id:
            queue.append(s)

    # Sort by priority descending
    queue.sort(key=lambda s: s["priority"], reverse=True)

    # Shuffle thread numbers
    thread_nums = list(range(1, len(queue)+1))
    random.shuffle(thread_nums)

    enrollments = load_enrollments()
    def enrolled_codes(sid):
        return [r["code"] for r in enrollments if r["id"] == sid]
    def dept_count(sid, dept):
        codes = enrolled_codes(sid)
        return sum(1 for code in codes if get_course(code) and get_course(code)["dept"] == dept)
    def elec_count(sid, dept):
        codes = enrolled_codes(sid)
        return sum(1 for code in codes if get_course(code) and get_course(code)["dept"] != dept)
    def total_count(sid):
        return sum(1 for r in enrollments if r["id"] == sid)

    results  = []
    # live seat tracking during simulation
    cur_enrolled = course_enrolled_count(course_code)
    total_seats  = COURSES[course_index]["seats"]
    new_records  = []  # to append to enrollments.txt after

    for i, student in enumerate(queue):
        sid       = student["id"]
        sdept     = student["dept"]
        is_own    = (sdept == course_dept)
        t_num     = thread_nums[i]
        status    = ""
        reason    = ""

        ec = enrolled_codes(sid)
        tc = total_count(sid)
        dc = dept_count(sid, sdept)
        xc = elec_count(sid, sdept)

        if course_code in ec:
            status = "BLOCKED"
            reason = "already enrolled in this course"
        elif cur_enrolled >= total_seats:
            status = "BLOCKED"
            reason = "semaphore blocked - no seats"
        elif tc >= 6:
            status = "BLOCKED"
            reason = "max 6 courses reached"
        elif is_own and dc >= 5:
            status = "BLOCKED"
            reason = "max 5 dept courses reached"
        elif not is_own and xc >= 1:
            status = "BLOCKED"
            reason = "max 1 elective already taken"
        elif not bankers_safe(course_index, sid, students_list):
            status = "BLOCKED"
            reason = "Banker's: unsafe state"
        else:
            cur_enrolled += 1
            status = "ENROLLED"
            reason = ""
            new_records.append({
                "id":   sid,
                "name": student["name"],
                "code": course_code,
                "studentDept": sdept
            })
            # update local counts so subsequent iterations see correct state
            enrollments.append({"id": sid, "name": student["name"],
                                 "code": course_code, "studentDept": sdept})

        results.append({
            "name":       student["name"],
            "priority":   student["priority"],
            "threadNum":  t_num,
            "status":     status,
            "reason":     reason,
            "seatsAfter": cur_enrolled,
            "totalSeats": total_seats,
            "isUser":     (sid == requester_id)
        })

    # Persist new enrollments
    if new_records:
        with open(ENROLL_F, "a") as f:
            for r in new_records:
                f.write(f"{r['id']},{r['name']},{r['code']},{r['studentDept']}\n")

    user_result = next((r for r in results if r["isUser"]), None)
    return results, user_result

# ── routes ───────────────────────────────────────────────────

@app.route("/")
def index():
    return send_from_directory("portal", "index.html")

@app.route("/api/login", methods=["POST"])
def login():
    data    = request.json
    name    = data.get("name","").strip()
    try:    sid = int(data.get("id", 0))
    except: return jsonify({"success": False, "error": "Invalid ID"})

    students = load_students()
    student  = next((s for s in students if s["id"]==sid and s["name"]==name), None)
    if not student:
        return jsonify({"success": False, "error": "Student not found. Check name and ID."})

    stats = build_student_stats(student)
    return jsonify({
        "success":  True,
        "student":  {**student, **stats,
                     "priorityLabel": PRIORITY_LABELS.get(student["priority"], "Unknown")}
    })

@app.route("/api/courses", methods=["GET"])
def get_courses():
    dept = request.args.get("dept", "")
    result = []
    for c in COURSES:
        if dept and c["dept"] != dept: continue
        enrolled = course_enrolled_count(c["code"])
        result.append({**c, "enrolled": enrolled, "available": c["seats"] - enrolled})
    return jsonify(result)

@app.route("/api/dashboard", methods=["GET"])
def dashboard():
    try: sid = int(request.args.get("id", 0))
    except: return jsonify({"error": "Invalid ID"})

    students = load_students()
    student  = next((s for s in students if s["id"]==sid), None)
    if not student: return jsonify({"error": "Not found"})

    stats = build_student_stats(student)

    # Enrich enrolled courses with full details
    enrolled_details = []
    for code in stats["enrolledCodes"]:
        c = get_course(code)
        if c:
            is_elective = c["dept"] != student["dept"]
            enrolled_details.append({**c, "isElective": is_elective})

    return jsonify({
        **student,
        **stats,
        "priorityLabel": PRIORITY_LABELS.get(student["priority"], "Unknown"),
        "enrolledDetails": enrolled_details
    })

@app.route("/api/enroll", methods=["POST"])
def enroll():
    data        = request.json
    try:    sid = int(data.get("studentId", 0))
    except: return jsonify({"success": False, "error": "Invalid ID"})

    course_code = data.get("courseCode","").strip()
    students    = load_students()
    student     = next((s for s in students if s["id"]==sid), None)
    if not student:
        return jsonify({"success": False, "error": "Student not found"})

    # Find course index
    course_index = next((i for i,c in enumerate(COURSES) if c["code"]==course_code), -1)
    if course_index == -1:
        return jsonify({"success": False, "error": "Course not found"})

    course    = COURSES[course_index]
    stats     = build_student_stats(student)
    is_own    = (student["dept"] == course["dept"])

    # Pre-checks
    if course_code in stats["enrolledCodes"]:
        return jsonify({"success": False, "error": "You are already enrolled in this course."})
    if stats["total"] >= 6:
        return jsonify({"success": False, "error": "You have reached the maximum of 6 courses."})
    if is_own and stats["deptCount"] >= 5:
        return jsonify({"success": False, "error": "You have already enrolled in 5 department courses."})
    if not is_own and stats["electiveCount"] >= 1:
        return jsonify({"success": False, "error": "You have already used your 1 elective slot."})

    enrolled_now = course_enrolled_count(course_code)
    if enrolled_now >= course["seats"]:
        return jsonify({"success": False, "error": "This course is full."})

    if not bankers_safe(course_index, sid, students):
        return jsonify({"success": False, "error": "Banker's Algorithm: system would enter unsafe state."})

    # Run priority simulation
    thread_log, user_result = simulate_priority_queue(
        course_index, course_code, course["dept"],
        sid, student["dept"], students
    )

    enrolled = user_result and user_result["status"] == "ENROLLED"
    return jsonify({
        "success":   enrolled,
        "threadLog": thread_log,
        "userResult": user_result,
        "message":  "Enrollment successful!" if enrolled else (user_result["reason"] if user_result else "Rejected")
    })

@app.route("/api/drop", methods=["POST"])
def drop():
    data        = request.json
    try:    sid = int(data.get("studentId", 0))
    except: return jsonify({"success": False, "error": "Invalid ID"})

    course_code = data.get("courseCode","").strip()
    codes       = enrolled_codes_for(sid)

    if course_code not in codes:
        return jsonify({"success": False, "error": "You are not enrolled in this course."})

    # Rewrite enrollments.txt without this one record
    records  = load_enrollments()
    skipped  = False
    new_lines = []
    for r in records:
        if not skipped and r["id"]==sid and r["code"]==course_code:
            skipped = True
            continue
        new_lines.append(f"{r['id']},{r['name']},{r['code']},{r['studentDept']}")

    with open(ENROLL_F, "w") as f:
        f.write("\n".join(new_lines))
        if new_lines: f.write("\n")

    course = get_course(course_code)
    return jsonify({
        "success": True,
        "message": f"Successfully dropped {course_code} - {course['title'] if course else ''}"
    })

if __name__ == "__main__":
    os.makedirs("portal", exist_ok=True)
    print("="*55)
    print("  University Enrollment Portal — Python Server")
    print("  Open your browser at: http://localhost:5000")
    print("="*55)
    app.run(debug=True, port=5000)
