#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <iomanip>
#include <thread>
#include <mutex>
#include <chrono>
#include <atomic>
#include <condition_variable>
using namespace std;

// ============================================================
//  STRUCTS
// ============================================================
struct Student {
    int    id;
    string name;
    string dept;
    int    priority;
    int    coursesEnrolled;      // total enrolled (max 6)
    int    deptCoursesEnrolled;  // from own dept (max 5)
    int    electiveEnrolled;     // from other dept (max 1)
    vector<string> enrolledCodes;
};

struct Course {
    string code;
    string title;
    string dept;
    int    totalSeats;
    int    enrolled;
};

struct Request {
    Student student;
    int     courseIndex;
};

struct ThreadResult {
    int    studentId;
    string studentName;
    int    priority;
    int    threadNum;
    bool   enrolled;
    string reason;
};

// ============================================================
//  GLOBALS
// ============================================================
vector<Student>      students;
vector<Course>       courses;
vector<ThreadResult> threadResults;

// ============================================================
//  SYNCHRONIZATION
// ============================================================
mutex              consoleMtx;
mutex              enrollMtx;
mutex              priorityGateMtx;
mutex              resultsMtx;
condition_variable priorityCV;
atomic<int>        currentTurn{0};

// ============================================================
//  COURSE CATALOGUE
// ============================================================
void initCourses() {
    courses.push_back({"CS101", "Intro to Programming",    "Computer",     30, 0});
    courses.push_back({"CS201", "Data Structures",         "Computer",     25, 0});
    courses.push_back({"CS301", "Operating Systems",       "Computer",     20, 0});
    courses.push_back({"CS401", "Computer Networks",       "Computer",     20, 0});
    courses.push_back({"CS501", "Artificial Intelligence", "Computer",     15, 0});

    courses.push_back({"EE101", "Circuit Analysis",        "Electrical",   30, 0});
    courses.push_back({"EE201", "Electronics",             "Electrical",   25, 0});
    courses.push_back({"EE301", "Signals & Systems",       "Electrical",   20, 0});
    courses.push_back({"EE401", "Power Systems",           "Electrical",   20, 0});
    courses.push_back({"EE501", "Control Systems",         "Electrical",   15, 0});

    courses.push_back({"ME101", "Engineering Mechanics",   "Mechanical",   30, 0});
    courses.push_back({"ME201", "Thermodynamics",          "Mechanical",   25, 0});
    courses.push_back({"ME301", "Fluid Mechanics",         "Mechanical",   20, 0});
    courses.push_back({"ME401", "Machine Design",          "Mechanical",   20, 0});
    courses.push_back({"ME501", "Manufacturing Processes", "Mechanical",   15, 0});

    courses.push_back({"MT101", "Intro to Mechatronics",   "Mechatronics", 30, 0});
    courses.push_back({"MT201", "Sensors & Actuators",     "Mechatronics", 25, 0});
    courses.push_back({"MT301", "Embedded Systems",        "Mechatronics", 20, 0});
    courses.push_back({"MT401", "Robotics",                "Mechatronics", 20, 0});
    courses.push_back({"MT501", "Industrial Automation",   "Mechatronics", 15, 0});
}

// ============================================================
//  FILE I/O
// ============================================================
void loadEnrollments() {
    ifstream file("enrollments.txt");
    if (!file.is_open()) return;

    string line;
    while (getline(file, line)) {
        if (line.empty()) continue;
        stringstream ss(line);
        string idStr, name, code, deptStr;
        getline(ss, idStr,   ',');
        getline(ss, name,    ',');
        getline(ss, code,    ',');
        getline(ss, deptStr, ',');

        int sid = stoi(idStr);

        for (auto &c : courses)
            if (c.code == code) { c.enrolled++; break; }

        for (auto &s : students) {
            if (s.id == sid) {
                s.coursesEnrolled++;
                s.enrolledCodes.push_back(code);

                string courseDept = "";
                for (auto &c : courses)
                    if (c.code == code) { courseDept = c.dept; break; }

                if (courseDept == s.dept) s.deptCoursesEnrolled++;
                else                      s.electiveEnrolled++;
                break;
            }
        }
    }
}

void saveEnrollmentRecord(const Student &s, const Course &c) {
    ofstream file("enrollments.txt", ios::app);
    file << s.id << "," << s.name << "," << c.code << "," << s.dept << "\n";
}

void loadStudents() {
    ifstream file("students.txt");
    if (!file.is_open()) {
        cout << "[WARNING] students.txt not found. Running with demo data.\n";
        students = {
            {1001, "Alice",   "Computer",     4, 0, 0, 0, {}},
            {1002, "Bob",     "Computer",     2, 0, 0, 0, {}},
            {1003, "Charlie", "Computer",     3, 0, 0, 0, {}},
            {1004, "Diana",   "Computer",     5, 0, 0, 0, {}},
            {1005, "Eve",     "Computer",     1, 0, 0, 0, {}},
            {1006, "Frank",   "Electrical",   3, 0, 0, 0, {}},
            {1007, "Grace",   "Mechanical",   4, 0, 0, 0, {}},
            {1008, "Hina",    "Mechatronics", 2, 0, 0, 0, {}},
        };
        return;
    }

    string line;
    while (getline(file, line)) {
        if (line.empty()) continue;
        stringstream ss(line);
        Student s;
        string temp;
        getline(ss, temp,   ','); s.id       = stoi(temp);
        getline(ss, s.name, ',');
        getline(ss, s.dept, ',');
        getline(ss, temp,   ','); s.priority = stoi(temp);
        s.coursesEnrolled     = 0;
        s.deptCoursesEnrolled = 0;
        s.electiveEnrolled    = 0;
        students.push_back(s);
    }
}

// ============================================================
//  AUTHENTICATE
// ============================================================
bool authenticate(int id, const string &name, Student &out) {
    for (auto &s : students)
        if (s.id == id && s.name == name) { out = s; return true; }
    return false;
}

// ============================================================
//  HELPERS
// ============================================================
bool alreadyEnrolled(const Student &s, const string &courseCode) {
    for (auto &code : s.enrolledCodes)
        if (code == courseCode) return true;
    return false;
}

bool alreadyEnrolledGlobal(int studentId, const string &courseCode) {
    for (auto &s : students)
        if (s.id == studentId)
            for (auto &code : s.enrolledCodes)
                if (code == courseCode) return true;
    return false;
}

// ============================================================
//  BANKER'S ALGORITHM  (max demand = 6)
// ============================================================
bool bankersAlgorithmSafe(int courseIndex, const Student &requester) {
    int n = (int)students.size();
    int m = (int)courses.size();
    const int MAX_DEMAND = 6;

    vector<int> available(m);
    for (int j = 0; j < m; j++)
        available[j] = courses[j].totalSeats - courses[j].enrolled;

    if (available[courseIndex] <= 0) return false;
    available[courseIndex]--;

    vector<int> need(n);
    for (int i = 0; i < n; i++)
        need[i] = MAX_DEMAND - students[i].coursesEnrolled;
    for (int i = 0; i < n; i++) {
        if (students[i].id == requester.id) {
            need[i] = max(0, need[i] - 1);
            break;
        }
    }

    int totalAvailable = 0;
    for (int j = 0; j < m; j++) totalAvailable += available[j];

    vector<bool> finished(n, false);
    int count = 0;
    while (count < n) {
        bool found = false;
        for (int i = 0; i < n; i++) {
            if (!finished[i] && need[i] <= totalAvailable) {
                totalAvailable += students[i].coursesEnrolled;
                finished[i] = true;
                count++;
                found = true;
            }
        }
        if (!found) break;
    }
    return (count == n);
}

// ============================================================
//  DISPLAY HELPERS
// ============================================================
void printSeparator(char c = '-', int w = 60) {
    cout << string(w, c) << "\n";
}

void displayDepartments() {
    cout << "\n";
    printSeparator('=');
    cout << "              SELECT A DEPARTMENT\n";
    printSeparator('=');
    cout << "  1. Computer Science\n";
    cout << "  2. Electrical Engineering\n";
    cout << "  3. Mechanical Engineering\n";
    cout << "  4. Mechatronics\n";
    printSeparator('=');
    cout << "Enter choice (1-4): ";
}

void displayCourses(const string &dept, vector<int> &idx) {
    idx.clear();
    cout << "\n";
    printSeparator('=');
    cout << "  COURSES  : " << dept << "\n";
    printSeparator('=');
    cout << left << setw(4) << "#"
         << setw(8)  << "Code"
         << setw(32) << "Title"
         << "Available Seats\n";
    printSeparator();
    int num = 0;
    for (int i = 0; i < (int)courses.size(); i++) {
        if (courses[i].dept == dept) {
            int avail = courses[i].totalSeats - courses[i].enrolled;
            cout << left << setw(4) << num
                 << setw(8)  << courses[i].code
                 << setw(32) << courses[i].title
                 << avail << "\n";
            idx.push_back(i);
            num++;
        }
    }
    printSeparator('=');
    cout << "Select course (0-4): ";
}

void displayEnrollmentSummary(const Student &s) {
    cout << "\n";
    printSeparator('=');
    cout << "  ENROLLMENT SUMMARY FOR: " << s.name << "\n";
    printSeparator('=');
    cout << "  Total courses enrolled : " << s.coursesEnrolled  << " / 6\n";
    cout << "  Dept courses           : " << s.deptCoursesEnrolled << " / 5\n";
    cout << "  Elective courses       : " << s.electiveEnrolled  << " / 1\n";
    if (!s.enrolledCodes.empty()) {
        cout << "  Enrolled in            : ";
        for (int i = 0; i < (int)s.enrolledCodes.size(); i++) {
            // Also show title alongside code
            string title = "";
            for (auto &c : courses)
                if (c.code == s.enrolledCodes[i]) { title = c.title; break; }
            cout << s.enrolledCodes[i] << " (" << title << ")";
            if (i < (int)s.enrolledCodes.size() - 1) cout << ", ";
        }
        cout << "\n";
    }
    printSeparator('=');
}

// ============================================================
//  THREAD FUNCTION
// ============================================================
void enrollThread(
    Student student,
    int     courseIndex,
    int     threadNum,
    int     myTurnOrder,
    int     loggedInId,
    string  courseCode)
{
    this_thread::sleep_for(chrono::milliseconds(5 * threadNum));

    {
        lock_guard<mutex> lock(consoleMtx);
        cout << "[THREAD " << setw(2) << threadNum << "] "
             << left << setw(16) << student.name
             << " (Priority " << student.priority << ")"
             << " - spawned, waiting for scheduler...\n";
    }

    {
        unique_lock<mutex> gate(priorityGateMtx);
        priorityCV.wait(gate, [&]() {
            return currentTurn.load() == myTurnOrder;
        });
    }

    // CRITICAL SECTION
    bool   success     = false;
    string reason;
    int    filledAfter = 0;
    int    totalSeats  = 0;

    {
        lock_guard<mutex> lock(enrollMtx);

        totalSeats = courses[courseIndex].totalSeats;
        bool isOwnDept = (student.dept == courses[courseIndex].dept);

        if (alreadyEnrolledGlobal(student.id, courseCode)) {
            reason = "already enrolled in this course";
        }
        else if (courses[courseIndex].enrolled >= totalSeats) {
            reason = "semaphore blocked - no seats";
        }
        else if (student.coursesEnrolled >= 6) {
            reason = "rejected - max 6 courses reached";
        }
        else if (isOwnDept && student.deptCoursesEnrolled >= 5) {
            reason = "rejected - max 5 dept courses reached";
        }
        else if (!isOwnDept && student.electiveEnrolled >= 1) {
            reason = "rejected - max 1 elective already taken";
        }
        else if (!bankersAlgorithmSafe(courseIndex, student)) {
            reason = "Banker's: unsafe state";
        }
        else {
            courses[courseIndex].enrolled++;
            for (auto &s : students) {
                if (s.id == student.id) {
                    s.coursesEnrolled++;
                    s.enrolledCodes.push_back(courseCode);
                    if (isOwnDept) s.deptCoursesEnrolled++;
                    else           s.electiveEnrolled++;
                    break;
                }
            }
            saveEnrollmentRecord(student, courses[courseIndex]);
            success     = true;
            filledAfter = courses[courseIndex].enrolled;
        }
    }
    // END CRITICAL SECTION

    {
        lock_guard<mutex> lock(consoleMtx);
        if (success) {
            cout << "[THREAD " << setw(2) << threadNum << "] "
                 << left << setw(16) << student.name
                 << " (Priority " << student.priority << ")"
                 << " - ENROLLED"
                 << "   (Seats filled: " << filledAfter
                 << "/" << totalSeats << ")\n";
        } else {
            cout << "[THREAD " << setw(2) << threadNum << "] "
                 << left << setw(16) << student.name
                 << " (Priority " << student.priority << ")"
                 << " - BLOCKED  (" << reason << ")\n";
        }
    }

    {
        lock_guard<mutex> lock(resultsMtx);
        threadResults.push_back({
            student.id, student.name, student.priority,
            threadNum, success, reason
        });
    }

    currentTurn.fetch_add(1);
    priorityCV.notify_all();
}

// ============================================================
//  DROP COURSE
// ============================================================
void dropCourse(Student &loggedIn) {

    // Sync from global
    for (auto &s : students)
        if (s.id == loggedIn.id) { loggedIn = s; break; }

    displayEnrollmentSummary(loggedIn);

    if (loggedIn.enrolledCodes.empty()) {
        cout << "\n  You are not enrolled in any courses. Nothing to drop.\n";
        return;
    }

    // Show droppable courses
    cout << "\n";
    printSeparator('=');
    cout << "  SELECT COURSE TO DROP\n";
    printSeparator('=');
    for (int i = 0; i < (int)loggedIn.enrolledCodes.size(); i++) {
        string code  = loggedIn.enrolledCodes[i];
        string title = "";
        string dept  = "";
        for (auto &c : courses)
            if (c.code == code) { title = c.title; dept = c.dept; break; }
        cout << "  " << i << ". [" << code << "] "
             << left << setw(32) << title
             << "(" << dept << ")\n";
    }
    printSeparator('=');
    cout << "Enter index to drop (-1 to cancel): ";

    int choice; cin >> choice;

    if (choice == -1) {
        cout << "  Drop cancelled.\n";
        return;
    }

    if (choice < 0 || choice >= (int)loggedIn.enrolledCodes.size()) {
        cout << "  ERROR: Invalid selection.\n";
        return;
    }

    string codeToDrop  = loggedIn.enrolledCodes[choice];
    string titleToDrop = "";
    string deptToDrop  = "";
    for (auto &c : courses)
        if (c.code == codeToDrop) { titleToDrop = c.title; deptToDrop = c.dept; break; }

    // Confirm
    cout << "\n  You are about to drop: [" << codeToDrop << "] "
         << titleToDrop << "\n";
    cout << "  Confirm? (y/n): ";
    char confirm; cin >> confirm;
    if (confirm != 'y' && confirm != 'Y') {
        cout << "  Drop cancelled.\n";
        return;
    }

    // Free the seat
    for (auto &c : courses)
        if (c.code == codeToDrop) { c.enrolled--; break; }

    // Update global student record
    for (auto &s : students) {
        if (s.id == loggedIn.id) {
            s.coursesEnrolled--;
            if (deptToDrop == s.dept) s.deptCoursesEnrolled--;
            else                      s.electiveEnrolled--;
            s.enrolledCodes.erase(
                remove(s.enrolledCodes.begin(), s.enrolledCodes.end(), codeToDrop),
                s.enrolledCodes.end()
            );
            break;
        }
    }

    // Rewrite enrollments.txt — remove only the first matching record
    ifstream fileIn("enrollments.txt");
    vector<string> lines;
    string line;
    bool skipped = false;
    while (getline(fileIn, line)) {
        if (line.empty()) continue;
        stringstream ss(line);
        string idStr, name, code, dept;
        getline(ss, idStr, ',');
        getline(ss, name,  ',');
        getline(ss, code,  ',');
        getline(ss, dept,  ',');

        if (!skipped && stoi(idStr) == loggedIn.id && code == codeToDrop) {
            skipped = true;
            continue; // skip this line (drop it)
        }
        lines.push_back(line);
    }
    fileIn.close();

    ofstream fileOut("enrollments.txt");
    for (auto &l : lines)
        fileOut << l << "\n";
    fileOut.close();

    // Sync loggedIn
    for (auto &s : students)
        if (s.id == loggedIn.id) { loggedIn = s; break; }

    cout << "\n";
    printSeparator('=', 60);
    cout << "  COURSE DROPPED SUCCESSFULLY\n";
    printSeparator('=', 60);
    cout << "  Dropped  : [" << codeToDrop << "] " << titleToDrop << "\n";
    cout << "  Seat has been freed back to the course pool.\n";
    cout << "\n  Updated enrollment status:\n";
    cout << "  Total: "    << loggedIn.coursesEnrolled     << "/6  |  ";
    cout << "Dept: "       << loggedIn.deptCoursesEnrolled << "/5  |  ";
    cout << "Elective: "   << loggedIn.electiveEnrolled    << "/1\n";
    printSeparator('=', 60);
}

// ============================================================
//  ENROLL
// ============================================================
void enroll(Student &loggedIn) {

    // Sync from global
    for (auto &s : students)
        if (s.id == loggedIn.id) { loggedIn = s; break; }

    displayEnrollmentSummary(loggedIn);

    if (loggedIn.coursesEnrolled >= 6) {
        cout << "\n  You have already enrolled in the maximum of 6 courses.\n";
        return;
    }

    // Step 1: Department
    displayDepartments();
    int d; cin >> d;
    string dept;
    if      (d == 1) dept = "Computer";
    else if (d == 2) dept = "Electrical";
    else if (d == 3) dept = "Mechanical";
    else             dept = "Mechatronics";

    // Elective / dept limit checks upfront
    if (dept != loggedIn.dept) {
        cout << "\n  [NOTE] You are selecting a course outside your department.\n";
        cout << "  This will count as your elective (max 1 allowed).\n";
        if (loggedIn.electiveEnrolled >= 1) {
            cout << "  [REJECTED] You have already used your 1 elective slot.\n";
            return;
        }
    } else {
        if (loggedIn.deptCoursesEnrolled >= 5) {
            cout << "\n  [REJECTED] You have already enrolled in 5 dept courses.\n";
            return;
        }
    }

    // Step 2: Course
    vector<int> idx;
    displayCourses(dept, idx);
    int choice; cin >> choice;

    if (choice < 0 || choice >= (int)idx.size()) {
        cout << "ERROR: Invalid course selection.\n";
        return;
    }

    int cindex = idx[choice];
    Course &selectedCourse = courses[cindex];

    cout << "\nYou selected: [" << selectedCourse.code << "] "
         << selectedCourse.title << "\n";

    // Step 3: Pre-checks
    cout << "\n";
    printSeparator('=');
    cout << "  PRE-ENROLLMENT CHECKS\n";
    printSeparator('=');

    if (alreadyEnrolled(loggedIn, selectedCourse.code)) {
        cout << "  [ALREADY ENROLLED]\n";
        cout << "  You are already enrolled in ["
             << selectedCourse.code << "] " << selectedCourse.title << ".\n";
        printSeparator('=');
        return;
    }

    if (selectedCourse.enrolled >= selectedCourse.totalSeats) {
        cout << "  [NO SEATS AVAILABLE]\n";
        cout << "  This course is full. Enrollment ABORTED.\n";
        printSeparator('=');
        return;
    }

    cout << "  No conflicts found. Running Banker's Algorithm...\n\n";
    printSeparator('=');

    // Step 4: Banker's
    cout << "  BANKER'S ALGORITHM  : SAFETY CHECK\n";
    printSeparator('=');
    cout << "  Max demand per student : 6 courses\n";
    cout << "  (5 from own dept + 1 elective from any other dept)\n\n";

    if (!bankersAlgorithmSafe(cindex, loggedIn)) {
        cout << "  [UNSAFE STATE DETECTED]\n";
        cout << "  Granting this request would cause deadlock.\n";
        cout << "  Enrollment process ABORTED.\n";
        printSeparator('=');
        return;
    }
    cout << "  Safe sequence found. System is in a SAFE STATE.\n";
    cout << "  Proceeding to thread scheduling...\n";
    printSeparator('=');

    // Step 5: Build priority queue
    vector<Request> requests;
    for (auto &st : students) {
        if (st.dept == selectedCourse.dept || st.id == loggedIn.id) {
            Request r;
            r.student     = st;
            r.courseIndex = cindex;
            requests.push_back(r);
        }
    }
    sort(requests.begin(), requests.end(),
         [](const Request &a, const Request &b) {
             return a.student.priority > b.student.priority;
         });

    int totalThreads = (int)requests.size();
    int availSeats   = selectedCourse.totalSeats - selectedCourse.enrolled;

    // Step 6: Spawn threads
    cout << "\n";
    printSeparator('=');
    cout << "  THREAD SIMULATION  :  CONCURRENT ENROLLMENT\n";
    printSeparator('=');
    cout << "  Spawning " << totalThreads << " threads for "
         << availSeats << " available seat(s)...\n";
    cout << "  Mutex locks critical section. Semaphore tracks seats.\n";
    cout << "  Priority scheduler releases threads highest-first.\n";
    cout << "  Rules: max 6 total | max 5 dept | max 1 elective\n\n";

    currentTurn.store(0);
    threadResults.clear();

    vector<int> threadNums(totalThreads);
    for (int i = 0; i < totalThreads; i++) threadNums[i] = i + 1;
    srand(static_cast<unsigned>(time(nullptr)));
    for (int i = totalThreads - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        swap(threadNums[i], threadNums[j]);
    }

    vector<thread> threads;
    for (int i = 0; i < totalThreads; i++) {
        threads.emplace_back(
            enrollThread,
            requests[i].student,
            cindex,
            threadNums[i],
            i,
            loggedIn.id,
            selectedCourse.code
        );
    }

    for (auto &t : threads) t.join();

    // Step 7: Final verdict
    bool   loggedInEnrolled = false;
    string rejectionReason  = "";
    for (auto &r : threadResults) {
        if (r.studentId == loggedIn.id) {
            if (r.enrolled) loggedInEnrolled = true;
            else            rejectionReason  = r.reason;
        }
    }

    // Sync loggedIn
    for (auto &s : students)
        if (s.id == loggedIn.id) { loggedIn = s; break; }

    cout << "\n";
    printSeparator('=', 60);
    cout << "  FINAL ENROLLMENT RESULT FOR: " << loggedIn.name << "\n";
    printSeparator('=', 60);

    if (loggedInEnrolled) {
        cout << "\n  ----------------------------------------------------------\n";
        cout <<   "       >> ENROLLMENT SUCCESSFUL <<\n";
        cout <<   "  Course  : " << selectedCourse.code
                                << " - " << selectedCourse.title << "\n";
        cout <<   "  Dept    : " << selectedCourse.dept << "\n";
        cout <<   "  Type    : "
             << (selectedCourse.dept == loggedIn.dept ? "Department Course" : "Elective") << "\n";
        cout <<   "  Record saved to enrollments.txt\n";
        cout <<   "  ----------------------------------------------------------\n";
        cout << "\n  Updated enrollment status:\n";
        cout << "  Total: "    << loggedIn.coursesEnrolled     << "/6  |  ";
        cout << "Dept: "       << loggedIn.deptCoursesEnrolled << "/5  |  ";
        cout << "Elective: "   << loggedIn.electiveEnrolled    << "/1\n";
        cout << "  ----------------------------------------------------------\n\n";
    } else {
        cout << "\n  ----------------------------------------------------------\n";
        cout <<   "       >> ENROLLMENT REJECTED <<\n";
        cout <<   "  Reason : " << rejectionReason << "\n";
        cout <<   "  ----------------------------------------------------------\n\n";
    }
    printSeparator('=', 60);
}

// ============================================================
//  MAIN
// ============================================================
int main() {
    initCourses();
    loadStudents();
    loadEnrollments();

    printSeparator('=', 60);
    cout << "        UNIVERSITY COURSE ENROLLMENT SYSTEM\n";
    cout << "    Multithreaded | Priority Scheduling | Banker's\n";
    cout << "         Algorithm | Mutex | Semaphore\n";
    cout << "       Max 6 Courses: 5 Dept + 1 Elective\n";
    printSeparator('=', 60);

    string name;
    int    id;

    cout << "Enter Full Name : ";
    getline(cin, name);

    cout << "Enter Student ID: ";
    cin >> id;

    Student loggedIn;
    if (!authenticate(id, name, loggedIn)) {
        cout << "\n[ERROR] Student not found. Check your name and ID.\n";
        return 1;
    }

    cout << "\n  Welcome, " << loggedIn.name
         << "!  (Department: " << loggedIn.dept
         << " | Priority: "    << loggedIn.priority << ")\n";

    // Session loop
    char again = 'y';
    while (again == 'y' || again == 'Y') {

        cout << "\n";
        printSeparator('=');
        cout << "  WHAT WOULD YOU LIKE TO DO?\n";
        printSeparator('=');
        cout << "  1. Enroll in a course\n";
        cout << "  2. Drop a course\n";
        cout << "  3. View enrollment summary\n";
        cout << "  4. Exit\n";
        printSeparator('=');
        cout << "Enter choice (1-4): ";

        int action; cin >> action;

        if (action == 1) {
            enroll(loggedIn);
        } else if (action == 2) {
            dropCourse(loggedIn);
        } else if (action == 3) {
            for (auto &s : students)
                if (s.id == loggedIn.id) { loggedIn = s; break; }
            displayEnrollmentSummary(loggedIn);
        } else {
            break;
        }

        if (action != 4) {
            cout << "\n  Continue? (y/n): ";
            cin >> again;
            cin.ignore();
        }
    }

    // Final session summary
    for (auto &s : students)
        if (s.id == loggedIn.id) { loggedIn = s; break; }

    cout << "\n";
    printSeparator('=', 60);
    cout << "  Session ended. Final enrollment summary:\n";
    printSeparator('=', 60);
    cout << "  Student  : " << loggedIn.name << "\n";
    cout << "  Total    : " << loggedIn.coursesEnrolled     << " / 6\n";
    cout << "  Dept     : " << loggedIn.deptCoursesEnrolled << " / 5\n";
    cout << "  Elective : " << loggedIn.electiveEnrolled    << " / 1\n";
    if (!loggedIn.enrolledCodes.empty()) {
        cout << "  Courses  : ";
        for (int i = 0; i < (int)loggedIn.enrolledCodes.size(); i++) {
            string title = "";
            for (auto &c : courses)
                if (c.code == loggedIn.enrolledCodes[i]) { title = c.title; break; }
            cout << loggedIn.enrolledCodes[i] << " (" << title << ")";
            if (i < (int)loggedIn.enrolledCodes.size() - 1) cout << "\n             ";
        }
        cout << "\n";
    }
    printSeparator('=', 60);

    return 0;
}
