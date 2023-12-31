#include "stdafx.h"

#include <thread>
#include "fmt/format.h"
#include "util.h"

#include "contest_types.h"
#include "solver_registry.h"
#include "solver_util.h"
#include "visual_editor.h"
#include "judge.h"


struct Xorshift {
  uint64_t x = 88172645463325252LL;
  void set_seed(unsigned seed, int rep = 100) {
    x = (seed + 1) * 10007;
    for (int i = 0; i < rep; i++) next_int();
  }
  unsigned next_int() {
    x = x ^ (x << 7);
    return x = x ^ (x >> 9);
  }
  unsigned next_int(unsigned mod) {
    x = x ^ (x << 7);
    x = x ^ (x >> 9);
    return x % mod;
  }
  unsigned next_int(unsigned l, unsigned r) {
    x = x ^ (x << 7);
    x = x ^ (x >> 9);
    return x % (r - l + 1) + l;
  }
  double next_double() {
    return double(next_int()) / std::numeric_limits<unsigned>::max();
  }
};

class K3Solver : public SolverBase {
public:
  K3Solver() {}

  using Pose = std::vector<Point>;

  struct SROI {
    integer x_min, y_min, x_max, y_max;
    SROI(integer x_min = 0, integer y_min = 0, integer x_max = 0, integer y_max = 0)
      : x_min(x_min), y_min(y_min), x_max(x_max), y_max(y_max) {}
    std::string str() const {
      return fmt::format("SROI [({}, {}) - ({}, {})]", x_min, y_min, x_max, y_max);
    }
    friend std::ostream& operator<<(std::ostream& o, const SROI& obj) {
      o << obj.str();
      return o;
    }
  };

  struct STrans;
  using STransPtr = std::shared_ptr<STrans>;
  struct STrans {
    enum class Type { MOVE, SLIDE, SWAP };
    Type type;
    double diff;
    STrans(Type type, double diff) : type(type), diff(diff) {}
  };

  struct SMove;
  using SMovePtr = std::shared_ptr<SMove>;
  struct SMove : STrans {
    integer vid;
    Point from, to;
    SMove(integer vid, const Point& from, const Point& to, double diff)
      : STrans(Type::MOVE, diff), vid(vid), from(from), to(to) {}
    std::string str() const {
      return fmt::format("SMove [vid={}, from=({}, {}), to=({}, {}), diff={}]", vid, from.first, from.second, to.first, to.second, diff);
    }
    friend std::ostream& operator<<(std::ostream& o, const SMove& obj) {
      o << obj.str();
      return o;
    }
    friend std::ostream& operator<<(std::ostream& o, const SMovePtr& obj) {
      o << obj->str();
      return o;
    }
  };

  struct SSwap;
  using SSwapPtr = std::shared_ptr<SSwap>;
  struct SSwap : STrans {
    integer uid, vid;
    SSwap(integer uid, integer vid, double diff)
      : STrans(Type::SWAP, diff), uid(uid), vid(vid) {}
    std::string str() const {
      return fmt::format("SSwap [uid={}, vid={}, diff={}]", uid, vid, diff);
    }
    friend std::ostream& operator<<(std::ostream& o, const SSwap& obj) {
      o << obj.str();
      return o;
    }
    friend std::ostream& operator<<(std::ostream& o, const SSwapPtr& obj) {
      o << obj->str();
      return o;
    }
  };

  struct SSlide;
  using SSlidePtr = std::shared_ptr<SSlide>;
  struct SSlide : STrans {
    int dir;
    SSlide(int dir, double diff) : STrans(Type::SLIDE, diff), dir(dir) {}
    std::string str() const {
      return fmt::format("SSlide [dir={}, diff={}]", dir, diff);
    }
    friend std::ostream& operator<<(std::ostream& o, const SSlide& obj) {
      o << obj.str();
      return o;
    }
    friend std::ostream& operator<<(std::ostream& o, const SSlidePtr& obj) {
      o << obj->str();
      return o;
    }
  };

  void initialize(const SolverArguments& args) {
    // master data
    prob = args.problem;
    bonuses = prob->bonuses;
    epsilon = prob->epsilon;
    edges = prob->edges;
    vertices_orig = prob->vertices;
    adjlist.resize(vertices_orig.size());
    for (auto [uid, vid] : edges) {
      auto u = vertices_orig[uid], v = vertices_orig[vid];
      d2_orig.push_back(distance2(u, v));
      adjlist[uid].push_back(vid);
      adjlist[vid].push_back(uid);
    }
    hole_polygon = prob->hole_polygon;
    build_polygon_map();
  }

  SROI calc_roi(const std::vector<Point>& points) const {
    integer x_min = std::numeric_limits<integer>::max();
    integer x_max = std::numeric_limits<integer>::min();
    integer y_min = std::numeric_limits<integer>::max();
    integer y_max = std::numeric_limits<integer>::min();
    for (auto [x, y] : points) {
      x_min = std::min(x_min, x);
      x_max = std::max(x_max, x);
      y_min = std::min(y_min, y);
      y_max = std::max(y_max, y);
    }
    return SROI(x_min, y_min, x_max, y_max);
  }

  void build_polygon_map() {
    polygon_roi = calc_roi(hole_polygon);
    auto& roi = polygon_roi;
    inner_polygon_map.resize(roi.y_max - roi.y_min + 1, std::vector<bool>(roi.x_max - roi.x_min + 1, false));
    for (integer y = roi.y_min; y <= roi.y_max; y++) {
      for (integer x = roi.x_min; x <= roi.x_max; x++) {
        Point p(x, y);
        if (contains(hole_polygon, p)) {
          inner_polygon_points.push_back(p);
          inner_polygon_map[y - roi.y_min][x - roi.x_min] = true;
        }
      }
    }
  }

  bool is_inside_polygon(integer x, integer y) const {
    auto& roi = polygon_roi;
    if (x < roi.x_min || x > roi.x_max || y < roi.y_min || y > roi.y_max) return false;
    return inner_polygon_map[y - roi.y_min][x - roi.x_min];
  }

  Point calc_representative_polygon_point(const std::vector<Point>& inner_polygon_points) const {
    int npoints = inner_polygon_points.size();
    double x_sum = 0.0, y_sum = 0.0;
    for (auto [x, y] : inner_polygon_points) {
      x_sum += x; y_sum += y;
    }
    x_sum /= npoints; y_sum /= npoints;
    double nearest_d2 = std::numeric_limits<double>::max();
    Point nearest_point;
    for (auto [x, y] : inner_polygon_points) {
      double d2 = (x_sum - x) * (x_sum - x) + (y_sum - y) * (y_sum - y);
      if (d2 < nearest_d2) {
        nearest_d2 = d2;
        nearest_point = Point(x, y);
      }
    }
    return nearest_point;
  }

  double calc_phys_cost(const Pose& pose) const {
    constexpr double eps = 1e-3;
    constexpr double coulomb_const = 1e4;
    constexpr double spring_const = 1.0;
    double cost = 0.0;
    for (int uid = 0; uid < pose.size(); uid++) {
      for (int vid = 0; vid < pose.size(); vid++) {
        // force: v -> u
        if (uid == vid) continue;
        cost += coulomb_const / (distance2(pose[uid], pose[vid]) + eps);
      }
    }
    return cost;
  }

  double evaluate(const Pose& pose) {
    SSolutionPtr sol = prob->create_solution(pose);
    auto res = judge(*prob, *sol);
    if (!res.fit_in_hole()) return std::numeric_limits<double>::max();
    //if (res.dislikes < best_dislikes) {
    //  // fit in hole の best
    //  best_dislikes = res.dislikes;
    //  LOG(INFO) << "best dislikes updated: " << best_dislikes;
    //  std::ofstream ofs("best_solution.json");
    //  ofs << prob->create_solution(pose)->json();
    //  LOG(INFO) << "best_solution.json saved.";
    //  ofs.close();
    //}
    double stretch_cost = 0.0, dislike_cost = res.dislikes;
    auto calc_violation = [this](integer orig_g2, integer mod_g2) {
      double window = epsilon * orig_g2 / 1000000.0;
      double penalty = 0.0;
      if (mod_g2 < orig_g2 - window) {
        penalty = orig_g2 - window - mod_g2;
      }
      if (orig_g2 + window < mod_g2) {
        penalty = mod_g2 - orig_g2 - window;
      }
      return penalty * penalty;
      //return abs(orig_g2 - mod_g2);
    };
    for (int eid : res.stretch_violating_edges) {
      auto [uid, vid] = edges[eid];
      auto u = pose[uid], v = pose[vid];
      stretch_cost += calc_violation(d2_orig[eid], distance2(u, v));
    }
    //for (int dislike : res.individual_dislikes) {
    //    dislike_cost += dislike * dislike;
    //}
    //return stretch_cost + res.dislikes * 3;
    // stretch_cost: 0.1 -> 0.9
    // dislike: 0.9 -> 0.1
    double weight = 2.0 * progress_rate;
    double phys_ratio = 0.0;
    double stretch_ratio = 1.0;
    double dislike_ratio = 0.0;
    return phys_ratio * calc_phys_cost(pose) + stretch_ratio * stretch_cost + dislike_ratio * dislike_cost;
  }

  SMovePtr calc_move_stat(Pose& pose, integer vid, const Point& to, double now_score) {
    auto from = pose[vid];
    pose[vid] = to;
    double new_score = evaluate(pose);
    pose[vid] = from;
    if (new_score == std::numeric_limits<double>::max()) return nullptr;
    return std::make_shared<SMove>(vid, from, to, new_score - now_score);
  }

  SSlidePtr calc_slide_stat(SPinnedIndex& pinned_index, const Pose& pose, int dir, double now_score) {
    static constexpr int di[] = { 0, -1, 0, 1 };
    static constexpr int dj[] = { 1, 0, -1, 0 };
    auto pose_cpy = pose;
    for(int idx : pinned_index.movable_indices) {
      auto& p = pose_cpy[idx];
      auto& [x, y] = p;
      if (!is_inside_polygon(x + dj[dir], y + di[dir])) return nullptr;
      x += dj[dir]; y += di[dir];
    }
    double new_score = evaluate(pose_cpy);
    if (new_score == std::numeric_limits<double>::max()) return nullptr;
    return std::make_shared<SSlide>(dir, new_score - now_score);
  }

  SSwapPtr calc_swap_stat(Pose& pose, int uid, int vid, double now_score) {
    swap(pose[uid], pose[vid]);
    double new_score = evaluate(pose);
    swap(pose[uid], pose[vid]);
    if (new_score == std::numeric_limits<double>::max()) return nullptr;
    return std::make_shared<SSwap>(uid, vid, new_score - now_score);
  }

  STransPtr create_random_trans(SPinnedIndex& pinned_index, Pose& pose, double now_score, Xorshift& rnd) {
    static constexpr int di[] = { 0, -1, 0, 1 };
    static constexpr int dj[] = { 1, 0, -1, 0 };
    int r = rnd.next_int(10);
    auto cands = pinned_index.movable_indices;
    if (r < 4) {
      int i = rnd.next_int(cands.size()), j;
      do {
        j = rnd.next_int(cands.size());
      } while (i == j);
      int uid = cands[i], vid = cands[j];
      return calc_swap_stat(pose, uid, vid, now_score);
    }
    if (r < 8) {
      // adjacent move
      int vid = cands[rnd.next_int(cands.size())];
      auto [x, y] = pose[vid];
      int dir = rnd.next_int(4);
      if (!is_inside_polygon(x + dj[dir], y + di[dir])) return nullptr;
      return calc_move_stat(pose, vid, Point(x + dj[dir], y + di[dir]), now_score);
    }
    if (r < 9) {
      // completely random warp
      int vid = cands[rnd.next_int(cands.size())];
      int to_id = rnd.next_int(inner_polygon_points.size());
      return calc_move_stat(pose, vid, inner_polygon_points[to_id], now_score);
    }
    else {
      return calc_slide_stat(pinned_index, pose, rnd.next_int(4), now_score);
    }
  }

  void move_vertex(std::vector<Point>& pose, integer vid, const Point& to) {
    pose[vid] = to;
  }

  void slide_all_vertices(SPinnedIndex& pinned_index, std::vector<Point>& pose, int dir) {
    static constexpr int di[] = { 0, -1, 0, 1 };
    static constexpr int dj[] = { 1, 0, -1, 0 };
    for(int idx : pinned_index.movable_indices) {
      auto& [x, y] = pose[idx];
      x += dj[dir]; y += di[dir];
    }
  }

  void transition(SPinnedIndex& pinned_index, std::vector<Point>& pose, STransPtr trans) {
    STrans::Type type = trans->type;
    switch (type) {
    case K3Solver::STrans::Type::MOVE:
    {
      SMovePtr mv = std::static_pointer_cast<SMove>(trans);
      move_vertex(pose, mv->vid, mv->to);
    }
    break;
    case K3Solver::STrans::Type::SLIDE:
    {
      SSlidePtr sl = std::static_pointer_cast<SSlide>(trans);
      slide_all_vertices(pinned_index, pose, sl->dir);
    }
    break;
    case K3Solver::STrans::Type::SWAP:
    {
      SSwapPtr sw = std::static_pointer_cast<SSwap>(trans);
      swap(pose[sw->uid], pose[sw->vid]);
      break;
    }
    default:
      break;
    }
  }

  std::vector<Point> scatter(std::vector<Point> pose, integer num_loop, Xorshift& rnd) {
    double now_score = evaluate(pose);
    for (integer loop = 0; loop < num_loop; loop++) {
      int vid = rnd.next_int(pose.size());
      int to_id = rnd.next_int(inner_polygon_points.size());
      SMovePtr mv = calc_move_stat(pose, vid, inner_polygon_points[to_id], now_score);
      if (mv) {
        move_vertex(pose, mv->vid, mv->to);
        now_score += mv->diff;
      }
      if (editor && loop % 1000 == 0) {
        LOG(INFO) << "loop = " << loop << ", score = " << now_score;
        editor->set_pose(prob->create_solution(pose));
        int c = editor->show(1);
      }
      loop++;
    }
    return pose;
  }

  SolverOutputs solve(const SolverArguments& args) override {
    initialize(args);

    int seed = 3;
    Xorshift rnd; rnd.set_seed(seed);

    constexpr bool scatter_mode = false;
    constexpr bool visualize = true;
    if (visualize) {
      editor = std::make_shared<SVisualEditor>(args.problem, "K3Solver", "visualize");
    }
    auto pose = (args.optional_initial_solution ? args.optional_initial_solution->vertices : vertices_orig);
    SPinnedIndex pinned_index(rng, pose.size(), editor);

    if (scatter_mode) {
      // initial state
      auto representative_point = calc_representative_polygon_point(inner_polygon_points);
      for (int i = 0; i < pose.size(); i++) move_vertex(pose, i, representative_point);
      pose = scatter(pose, 10000, rnd);
    }
    else {
      editor->set_pose(prob->create_solution(pose));
      while (true) {
        int c = editor->show(1);
        if (c == 27) {
          break;
        }
      }
      pose = editor->get_pose()->vertices;
      pinned_index.update_movable_index();
    }

    auto get_temp = [](double stemp, double etemp, double loop, double num_loop) {
      return etemp + (stemp - etemp) * (num_loop - loop) / num_loop;
    };

    progress_rate = 0.0;
    double now_score = evaluate(pose);
    LOG(INFO) << "initial score = " << now_score;
    integer loop = 0, num_loop = 1000000, accepted = 0;
    for (integer loop = 0; loop < num_loop; loop++) {
      if (editor && loop % 1000 == 0) {
        progress_rate = double(loop) / num_loop;
        now_score = evaluate(pose);
        LOG(INFO)
          << "loop = " << loop
          << ", progress_rate = " << progress_rate
          << ", accept_rate = " << double(accepted) / loop
          << ", score = " << now_score;
        editor->set_pose(prob->create_solution(pose));
        if (auto show_result = editor->show(1); show_result.edit_result) {
          pose = show_result.edit_result->pose_after_edit->vertices;
          pinned_index.update_movable_index();
        }
      }
      STransPtr trans = create_random_trans(pinned_index, pose, now_score, rnd);
      if (!trans) continue;
      double temp = get_temp(10.0, 0.0, loop, num_loop);
      double prob = exp(-trans->diff / temp);
      if (rnd.next_double() < prob) {
        accepted++;
        transition(pinned_index, pose, trans);
        now_score += trans->diff;
      }
    }
    // do nothing.
    SolverOutputs ret;
    ret.solution = prob->create_solution(pose);
    return ret;
  }

private:
  // master data
  SProblemPtr prob;
  std::vector<SBonus> bonuses;
  integer epsilon;
  std::vector<Edge> edges;
  std::vector<std::vector<integer>> adjlist;
  std::vector<integer> d2_orig;
  std::vector<Point> vertices_orig;
  std::vector<Point> hole_polygon;
  SROI polygon_roi;
  std::vector<std::vector<bool>> inner_polygon_map;
  std::vector<Point> inner_polygon_points;
  // for evaluation
  double progress_rate;
  //integer best_dislikes = std::numeric_limits<integer>::max();
  // for visualize
  SVisualEditorPtr editor;
  std::mt19937 rng;
};

REGISTER_SOLVER("K3Solver", K3Solver);
// vim:ts=2 sw=2 sts=2 et ci

