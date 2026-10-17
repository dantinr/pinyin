#include "pinyin/lexicon.hpp"
#include "pinyin/input_session.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
int checks = 0;
void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

bool contains(const pinyin::Lexicon& lexicon, const std::string& query,
              const std::string& text, const std::string& pronunciation) {
    const auto candidates = lexicon.lookup(query, {}, 128);
    return std::any_of(candidates.begin(), candidates.end(), [&](const auto& candidate) {
        return candidate.text == text && candidate.pronunciation == pronunciation;
    });
}

// Lexicon::load already validates UTF-8; count its code points for this dataset's
// one-written-character-per-syllable convention, including an explicit er for 儿.
std::size_t character_count(const std::string& text) {
    return static_cast<std::size_t>(std::count_if(text.begin(), text.end(), [](unsigned char byte) {
        return (byte & 0xc0) != 0x80;
    }));
}

struct Sample { const char* query; const char* text; const char* pronunciation; };
const Sample samples[] = {
    {"shishi", "试试", "shi shi"},
    {"ss", "试试", "shi shi"},
    {"shiyishi", "试一试", "shi yi shi"},
    {"xiangxiang", "想想", "xiang xiang"},
    {"dengdeng", "等等", "deng deng"},
    {"wenwen", "问问", "wen wen"},
    {"shuoyishuo", "说一说", "shuo yi shuo"},
    {"tingyiting", "听一听", "ting yi ting"},
    {"changyichang", "尝一尝", "chang yi chang"},
    {"chayicha", "查一查", "cha yi cha"},
    {"bangbangmang", "帮帮忙", "bang bang mang"},
    {"manmanlai", "慢慢来", "man man lai"},
    {"shishi", "实时", "shi shi"},
    {"shishi", "时时", "shi shi"},
    {"shishi", "时事", "shi shi"},
    {"shishi", "适时", "shi shi"},
    {"shishi", "逝世", "shi shi"},
    {"shishi", "实事", "shi shi"},
    {"mingling", "命令", "ming ling"},
    {"xiexie", "谢谢", "xie xie"},
    {"tianqi", "天气", "tian qi"},
    {"jiating", "家庭", "jia ting"},
    {"jidan", "鸡蛋", "ji dan"},
    {"shufu", "舒服", "shu fu"},
    {"zixingche", "自行车", "zi xing che"},
    {"zuoye", "作业", "zuo ye"},
    {"tushuguan", "图书馆", "tu shu guan"},
    {"hetong", "合同", "he tong"},
    {"huiyi", "会议", "hui yi"},
    {"diannao", "电脑", "dian nao"},
    {"jianpan", "键盘", "jian pan"},
    {"liulanqi", "浏览器", "liu lan qi"},
    {"dianziyoujian", "电子邮件", "dian zi you jian"},
    {"yuandaima", "源代码", "yuan dai ma"},
    {"pinyinshurufa", "拼音输入法", "pin yin shu ru fa"},
    {"yinsibaohu", "隐私保护", "yin si bao hu"},
    {"ditie", "地铁", "di tie"},
    {"beijing", "北京", "bei jing"},
    {"xi'an", "西安", "xi an"},
    {"lvcha", "绿茶", "lv cha"},
    {"nühai", "女孩", "nv hai"},
    {"LÜSE", "绿色", "lv se"},
    {"lvyou", "旅游", "lv you"},
    {"nver", "女儿", "nv er"},
    {"xiexienindebangzhu", "谢谢您的帮助", "xie xie nin de bang zhu"},
    {"zaoshanghao", "早上好", "zao shang hao"},
    {"yanjing", "眼睛", "yan jing"},
    {"yinhangka", "银行卡", "yin hang ka"},
    {"buzhaoji", "不着急", "bu zhao ji"},
    {"chongqing", "重庆", "chong qing"},
    {"chonglai", "重来", "chong lai"},
    {"zhongliang", "重量", "zhong liang"},
    {"yinhang", "银行", "yin hang"},
    {"xingzou", "行走", "xing zou"},
    {"hangshu", "行数", "hang shu"},
    {"yinyue", "音乐", "yin yue"},
    {"kuaile", "快乐", "kuai le"},
    {"shuijiao", "睡觉", "shui jiao"},
    {"ganjue", "感觉", "gan jue"},
    {"pianyi", "便宜", "pian yi"},
    {"fangbian", "方便", "fang bian"},
    {"zhaoyang", "朝阳", "zhao yang"},
    {"chaoyang", "朝阳", "chao yang"},
    {"tonghang", "同行", "tong hang"},
    {"tongxing", "同行", "tong xing"},
    {"renshen", "人参", "ren shen"},
    {"canjia", "参加", "can jia"},
    {"cenci", "参差", "cen ci"},
    {"chenzhi", "称职", "chen zhi"},
    {"duichen", "对称", "dui chen"},
    {"juese", "角色", "jue se"},
    {"zhujue", "主角", "zhu jue"},
    {"chuchai", "出差", "chu chai"},
    {"tiaoshi", "调试", "tiao shi"},
    {"diaoyong", "调用", "diao yong"},
    {"changcheng", "长城", "chang cheng"},
    {"chengzhang", "成长", "cheng zhang"},
    {"xizang", "西藏", "xi zang"},
    {"shoucang", "收藏", "shou cang"},
    {"zhuanji", "传记", "zhuan ji"},
    {"chuanbo", "传播", "chuan bo"},
    {"jiyu", "给予", "ji yu"},
    {"jiaoshu", "教书", "jiao shu"},
    {"zhaoji", "着急", "zhao ji"},
    {"zhuoshou", "着手", "zhuo shou"},
    {"zhi", "滞", "zhi"},
    {"tingzhi", "停滞", "ting zhi"},
    {"tingzhibuqian", "停滞不前", "ting zhi bu qian"},
    {"zhihou", "滞后", "zhi hou"},
    {"zhiliu", "滞留", "zhi liu"},
    {"zhenzhuo", "斟酌", "zhen zhuo"},
    {"ganga", "尴尬", "gan ga"},
    {"cankui", "惭愧", "can kui"},
    {"qieyi", "惬意", "qie yi"},
    {"qiaocui", "憔悴", "qiao cui"},
    {"juejiang", "倔强", "jue jiang"},
    {"zhiniu", "执拗", "zhi niu"},
    {"yunhan", "蕴含", "yun han"},
    {"yunniang", "酝酿", "yun niang"},
    {"qianyi", "迁移", "qian yi"},
    {"qianyimohua", "潜移默化", "qian yi mo hua"},
    {"cencibuqi", "参差不齐", "cen ci bu qi"},
    {"xian", "癣", "xian"},
    {"wanlaijuji", "万籁俱寂", "wan lai ju ji"},
    {"ruhe", "如何", "ru he"},
    {"zhantie", "粘贴", "zhan tie"},
    {"huomian", "和面", "huo mian"},
    {"caifeng", "裁缝", "cai feng"},
    {"fengxi", "缝隙", "feng xi"},
    {"junlie", "龟裂", "jun lie"},
    {"qieerbushe", "锲而不舍", "qie er bu she"},
    {"zenghen", "憎恨", "zeng hen"},
    {"conglong", "葱茏", "cong long"},
    {"baoxiang", "爆香", "bao xiang"},
    {"ruyuanyichang", "如愿以偿", "ru yuan yi chang"},
    // Computing vocabulary: cover every topic and contextual polyphones.
    {"jisuanjizuchengyuanli", "计算机组成原理", "ji suan ji zu cheng yuan li"},
    {"fengnuoyimanjiegou", "冯诺依曼结构", "feng nuo yi man jie gou"},
    {"huancunhang", "缓存行", "huan cun hang"},
    {"ruipin", "睿频", "rui pin"},
    {"gutaiyingpan", "固态硬盘", "gu tai ying pan"},
    {"yonghutai", "用户态", "yong hu tai"},
    {"xitongdiaoyong", "系统调用", "xi tong diao yong"},
    {"jinchengdiaodu", "进程调度", "jin cheng diao du"},
    {"xianchengzuse", "线程阻塞", "xian cheng zu se"},
    {"kechongrusuo", "可重入锁", "ke chong ru suo"},
    {"yin yong ji shu", "引用计数", "yin yong ji shu"},
    {"wenjianchongmingming", "文件重命名", "wen jian chong ming ming"},
    {"wangluoyongse", "网络拥塞", "wang luo yong se"},
    {"zhishutuibi", "指数退避", "zhi shu tui bi"},
    {"shengmingshibiancheng", "声明式编程", "sheng ming shi bian cheng"},
    {"hanshuchongzai", "函数重载", "han shu chong zai"},
    {"duochongjicheng", "多重继承", "duo chong ji cheng"},
    {"hongheishu", "红黑树", "hong hei shu"},
    {"bulongguolvqi", "布隆过滤器", "bu long guo lv qi"},
    {"tanhuanfenxi", "摊还分析", "tan huan fen xi"},
    {"jishibianyi", "即时编译", "ji shi bian yi"},
    {"tiaoshifuhao", "调试符号", "tiao shi fu hao"},
    {"dan hang zhu shi", "单行注释", "dan hang zhu shi"},
    {"hangfugai", "行覆盖", "hang fu gai"},
    {"mo hu ce shi", "模糊测试", "mo hu ce shi"},
    {"jiaohushibianji", "交互式变基", "jiao hu shi bian ji"},
    {"yuyanfuwuqi", "语言服务器", "yu yan fu wu qi"},
    {"shejimoshi", "设计模式", "she ji mo shi"},
    {"xiangyingshibuju", "响应式布局", "xiang ying shi bu ju"},
    {"nianxingdingwei", "粘性定位", "nian xing ding wei"},
    {"fuwuduanxuanran", "服务端渲染", "fu wu duan xuan ran"},
    {"yuanchengguochengdiaoyong", "远程过程调用", "yuan cheng guo cheng diao yong"},
    {"shujukuguanlixitong", "数据库管理系统", "shu ju ku guan li xi tong"},
    {"shiwugelijibie", "事务隔离级别", "shi wu ge li ji bie"},
    {"hangsuo", "行锁", "hang suo"},
    {"duojihuancun", "多级缓存", "duo ji huan cun"},
    {"fenbushixitong", "分布式系统", "fen bu shi xi tong"},
    {"liuliangxuefeng", "流量削峰", "liu liang xue feng"},
    {"rongqibianpai", "容器编排", "rong qi bian pai"},
    {"gaojingyuzhi", "告警阈值", "gao jing yu zhi"},
    {"jinsiquebushu", "金丝雀部署", "jin si que bu shu"},
    {"xingnengpouxi", "性能剖析", "xing neng pou xi"},
    {"diouhe", "低耦合", "di ou he"},
    {"jue se quan xian", "角色权限", "jue se quan xian"},
    {"miyuejiaohuan", "密钥交换", "mi yue jiao huan"},
    {"gongyuejiami", "公钥加密", "gong yue jia mi"},
    {"siyuejiami", "私钥加密", "si yue jia mi"},
    {"moxingweitiao", "模型微调", "mo xing wei tiao"},
    {"jian suozengqiangshengcheng", "检索增强生成", "jian suo zeng qiang sheng cheng"},
    {"shujuxueyuan", "数据血缘", "shu ju xue yuan"},
    {"guangshanhua", "光栅化", "guang shan hua"},
    {"zhuoseqi", "着色器", "zhuo se qi"},
    {"yubianjiwenben", "预编辑文本", "yu bian ji wen ben"},
    {"maikuantiaozhi", "脉宽调制", "mai kuan tiao zhi"},
    {"jioujiaoyan", "奇偶校验", "ji ou jiao yan"},
    {"jsjwl", "计算机网络", "ji suan ji wang luo"},
    {"sjkglxt", "数据库管理系统", "shu ju ku guan li xi tong"},
    {"blglq", "布隆过滤器", "bu long guo lv qi"},
    {"fbsxt", "分布式系统", "fen bu shi xi tong"},
    {"xtdy", "系统调用", "xi tong diao yong"},
    {"maikuantzh", "脉宽调制", "mai kuan tiao zhi"},
};
const Sample priorities[] = {
    {"shishi", "试试", "shi shi"},
    {"mingling", "命令", "ming ling"},
    {"nihao", "你好", "ni hao"},
    {"shouji", "手机", "shou ji"},
    {"sheji", "设计", "she ji"},
    {"huifu", "回复", "hui fu"},
    {"xiaoxi", "消息", "xiao xi"},
    {"keyi", "可以", "ke yi"},
    {"zhidao", "知道", "zhi dao"},
    {"zhongqing", "钟情", "zhong qing"},
};
const Sample daily_samples[] = {
    {"jiatingshenghuo", "家庭生活", "jia ting sheng huo"},
    {"zhinv", "侄女", "zhi nv"},
    {"zhusuo", "住所", "zhu suo"},
    {"shoucangpin", "收藏品", "shou cang pin"},
    {"dianbingcheng", "电饼铛", "dian bing cheng"},
    {"mabu", "抹布", "ma bu"},
    {"pengren", "烹饪", "peng ren"},
    {"guotie", "锅贴", "guo tie"},
    {"luobo", "萝卜", "luo bo"},
    {"biqi", "荸荠", "bi qi"},
    {"niunan", "牛腩", "niu nan"},
    {"guiyuan", "桂圆", "gui yuan"},
    {"guofu", "果脯", "guo fu"},
    {"hongcha", "红茶", "hong cha"},
    {"ganlanyou", "橄榄油", "gan lan you"},
    {"maonidayi", "毛呢大衣", "mao ni da yi"},
    {"niuzaiwaitao", "牛仔外套", "niu zai wai tao"},
    {"gongzai", "公仔", "gong zai"},
    {"baozaifan", "煲仔饭", "bao zai fan"},
    {"yundou", "熨斗", "yun dou"},
    {"hufu", "护肤", "hu fu"},
    {"gouwuche", "购物车", "gou wu che"},
    {"wanggou", "网购", "wang gou"},
    {"zanqian", "攒钱", "zan qian"},
    {"lepai", "肋排", "le pai"},
    {"chailv", "差旅", "chai lv"},
    {"zhuanhang", "转行", "zhuan hang"},
    {"longtang", "弄堂", "long tang"},
    {"zhaji", "闸机", "zha ji"},
    {"banche", "班车", "ban che"},
    {"chepaihao", "车牌号", "che pai hao"},
    {"ruanzuo", "软座", "ruan zuo"},
    {"xinglie", "行李额", "xing li e"},
    {"danrenjian", "单人间", "dan ren jian"},
    {"bujiao", "补觉", "bu jiao"},
    {"yanjian", "眼睑", "yan jian"},
    {"chongfeng", "重逢", "chong feng"},
    {"bise", "鼻塞", "bi se"},
    {"chufangyao", "处方药", "chu fang yao"},
    {"duoyun", "多云", "duo yun"},
    {"jiuyi", "就医", "jiu yi"},
    {"tiyuyundong", "体育运动", "ti yu yun dong"},
    {"hugong", "护工", "hu gong"},
    {"heai", "和蔼", "he ai"},
    {"tanran", "坦然", "tan ran"},
    {"mingliao", "明了", "ming liao"},
    {"chenzhuo", "沉着", "chen zhuo"},
    {"fengsheng", "丰盛", "feng sheng"},
    {"diaogang", "调岗", "diao gang"},
    {"qiyue", "器乐", "qi yue"},
    {"shengyue", "声乐", "sheng yue"},
    {"peiyue", "配乐", "pei yue"},
    {"yichana", "一刹那", "yi cha na"},
    {"heying", "合影", "he ying"},
    {"liuwan", "遛弯", "liu wan"},
    {"huizhen", "会诊", "hui zhen"},
    {"hunqing", "婚庆", "hun qing"},
    {"dingfang", "订房", "ding fang"},
    {"gongjiaoka", "公交卡", "gong jiao ka"},
    {"buxiu", "补休", "bu xiu"},
    {"jiaoyufangshi", "教育方式", "jiao yu fang shi"},
    {"jiaju", "家居", "jia ju"},
    {"gonggongjiaotong", "公共交通", "gong gong jiao tong"},
    {"jtsh", "家庭生活", "jia ting sheng huo"},
    {"jyfs", "教育方式", "jiao yu fang shi"},
    {"ggjt", "公共交通", "gong gong jiao tong"},
    {"jj", "家居", "jia ju"},
};
const Sample common_samples[] = {
    {"haolei", "好嘞", "hao lei"},
    {"meicuo", "没错", "mei cuo"},
    {"nanshuo", "难说", "nan shuo"},
    {"kebushi", "可不是", "ke bu shi"},
    {"yuanlairuci", "原来如此", "yuan lai ru ci"},
    {"huanjuhuashuo", "换句话说", "huan ju hua shuo"},
    {"huashuohuilai", "话说回来", "hua shuo hui lai"},
    {"shuoshihua", "说实话", "shuo shi hua"},
    {"laojia", "劳驾", "lao jia"},
    {"tanshuaideshuo", "坦率地说", "tan shuai de shuo"},
    {"yongbuzhao", "用不着", "yong bu zhao"},
    {"fanbuzhao", "犯不着", "fan bu zhao"},
    {"bielaiwuyang", "别来无恙", "bie lai wu yang"},
    {"dabashou", "搭把手", "da ba shou"},
    {"shishikan", "试试看", "shi shi kan"},
    {"wenwenkan", "问问看", "wen wen kan"},
    {"pianpian", "偏偏", "pian pian"},
    {"qiaqia", "恰恰", "qia qia"},
    {"bujin", "不仅", "bu jin"},
    {"youqi", "尤其", "you qi"},
    {"wulunruhe", "无论如何", "wu lun ru he"},
    {"jibianruci", "即便如此", "ji bian ru ci"},
    {"genghekuang", "更何况", "geng he kuang"},
    {"yucitongshi", "与此同时", "yu ci tong shi"},
    {"zhaozhaomumu", "朝朝暮暮", "zhao zhao mu mu"},
    {"gesanchawu", "隔三差五", "ge san cha wu"},
    {"wosa", "我仨", "wo sa"},
    {"lingyiban", "另一半", "ling yi ban"},
    {"baojin", "抱紧", "bao jin"},
    {"zuanjin", "攥紧", "zuan jin"},
    {"zhangye", "长叶", "zhang ye"},
    {"dianjiao", "踮脚", "dian jiao"},
    {"changyang", "徜徉", "chang yang"},
    {"quansuo", "蜷缩", "quan suo"},
    {"chayaoshi", "插钥匙", "cha yao shi"},
    {"jijin", "系紧", "ji jin"},
    {"sicun", "思忖", "si cun"},
    {"wangbuliao", "忘不了", "wang bu liao"},
    {"qianyishi", "潜意识", "qian yi shi"},
    {"koutouchan", "口头禅", "kou tou chan"},
    {"tuotie", "妥帖", "tuo tie"},
    {"xinshichongchong", "心事重重", "xin shi chong chong"},
    {"xianxi", "嫌隙", "xian xi"},
    {"koujue", "口角", "kou jue"},
    {"ouqi", "怄气", "ou qi"},
    {"dingdiao", "定调", "ding diao"},
    {"yandang", "延宕", "yan dang"},
    {"pianpo", "偏颇", "pian po"},
    {"hangju", "行距", "hang ju"},
    {"sidongfeidong", "似懂非懂", "si dong fei dong"},
    {"paogenwendi", "刨根问底", "pao gen wen di"},
    {"zuzhang", "组长", "zu zhang"},
    {"jiaogao", "校稿", "jiao gao"},
    {"jiediao", "借调", "jie diao"},
    {"shangsha", "商厦", "shang sha"},
    {"maolilv", "毛利率", "mao li lv"},
    {"yuankuan", "援款", "yuan kuan"},
    {"zhenzai", "赈灾", "zhen zai"},
    {"mingmabiaojia", "明码标价", "ming ma biao jia"},
    {"chedadeng", "车大灯", "che da deng"},
    {"shacheyou", "刹车油", "sha che you"},
    {"xuezhang", "学长", "xue zhang"},
    {"diaoke", "调课", "diao ke"},
    {"kaoyan", "考研", "kao yan"},
    {"suitangkao", "随堂考", "sui tang kao"},
    {"zhongdiangong", "钟点工", "zhong dian gong"},
    {"famen", "发闷", "fa men"},
    {"mengyan", "梦魇", "meng yan"},
    {"liuguanshi", "留观室", "liu guan shi"},
    {"hanpian", "含片", "han pian"},
    {"ruannuo", "软糯", "ruan nuo"},
    {"xiuqi", "修葺", "xiu qi"},
    {"chuman", "除螨", "chu man"},
    {"peishenyuan", "陪审员", "pei shen yuan"},
    {"rongqueshouli", "容缺受理", "rong que shou li"},
    {"qungonggao", "群公告", "qun gong gao"},
    {"dajiyue", "打击乐", "da ji yue"},
    {"xianyue", "弦乐", "xian yue"},
    {"guanyue", "管乐", "guan yue"},
    {"minyue", "民乐", "min yue"},
    {"xiyangyue", "西洋乐", "xi yang yue"},
    {"qingyinyue", "轻音乐", "qing yin yue"},
    {"liuxingyue", "流行乐", "liu xing yue"},
    {"gudianyue", "古典乐", "gu dian yue"},
    {"kancha", "勘查", "kan cha"},
    {"zizhongxunlian", "自重训练", "zi zhong xun lian"},
    {"wusong", "雾凇", "wu song"},
    {"chana", "刹那", "cha na"},
    {"youhei", "黝黑", "you hei"},
    {"quhei", "黢黑", "qu hei"},
    {"xiaoqi", "小憩", "xiao qi"},
    {"zhelishui", "啫喱水", "zhe li shui"},
    {"tianzhukui", "天竺葵", "tian zhu kui"},
    {"zhizihua", "栀子花", "zhi zi hua"},
    {"buzhanguo", "不粘锅", "bu zhan guo"},
    {"ciba", "糍粑", "ci ba"},
    {"roujiamo", "肉夹馍", "rou jia mo"},
    {"chengzi", "蛏子", "cheng zi"},
    {"zhuroufu", "猪肉脯", "zhu rou fu"},
    {"xijiaomanyan", "细嚼慢咽", "xi jiao man yan"},
    {"faqia", "发卡", "fa qia"},
    {"buchajia", "补差价", "bu cha jia"},
    {"hjhs", "换句话说", "huan ju hua shuo"},
    {"zzmm", "朝朝暮暮", "zhao zhao mu mu"},
    {"xscc", "心事重重", "xin shi chong chong"},
    {"rqsl", "容缺受理", "rong que shou li"},
    {"xjmy", "细嚼慢咽", "xi jiao man yan"},
};
}

int wmain(int argc, wchar_t* argv[]) {
    try {
        if (argc != 2) throw std::runtime_error("expected production dictionary path");
        pinyin::Lexicon lexicon;
        const auto start = std::chrono::steady_clock::now();
        lexicon.load(argv[1]);
        const auto load_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();

        std::ifstream input(std::filesystem::path(argv[1]), std::ios::binary);
        if (!input) throw std::runtime_error("cannot read dictionary for audit");
        std::set<std::pair<std::string, std::string>> readings;
        std::map<std::string, std::set<std::string>> single_readings;
        std::size_t rows = 0, line_number = 0;
        std::set<std::string> computing_words;
        std::set<std::string> daily_words;
        std::set<std::string> common_words, earlier_texts;
        bool computing_section = false, daily_section = false;
        bool common_section = false, common_started = false;
        for (std::string line; std::getline(input, line);) {
            ++line_number;
            if (line_number == 1 && line.compare(0, 3, "\xef\xbb\xbf") == 0) line.erase(0, 3);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            if (line.front() == '#') {
                if (line == "# 计算机与编程词汇扩充（2026-10-13）") computing_section = true;
                if (line == "# 计算机词汇所需补充单字读音") computing_section = false;
                if (line == "# 日常词汇扩充（2026-10-13）") daily_section = true;
                if (line == "# 日常词汇所需补充单字读音") daily_section = false;
                if (line == "# 常用词汇扩充（2026-10-17）") common_section = common_started = true;
                if (line == "# 常用词汇所需补充单字读音（2026-10-17）") common_section = false;
                continue;
            }
            const auto first = line.find('\t'), second = line.find('\t', first + 1);
            const auto text = line.substr(0, first);
            const auto pronunciation = line.substr(first + 1, second - first - 1);
            const auto context = " at line " + std::to_string(line_number) + ": " + text;
            if (pronunciation != pinyin::normalize_pronunciation(pronunciation))
                throw std::runtime_error("noncanonical pronunciation" + context);
            const auto syllables = static_cast<std::size_t>(
                std::count(pronunciation.begin(), pronunciation.end(), ' ') + 1);
            if (character_count(text) != syllables)
                throw std::runtime_error("character/syllable count mismatch" + context);
            if (!readings.emplace(text, pronunciation).second)
                throw std::runtime_error("duplicate word/reading" + context);
            if (character_count(text) == 1) single_readings[text].insert(pronunciation);
            if (computing_section && character_count(text) > 1) computing_words.insert(text);
            if (daily_section && character_count(text) > 1) daily_words.insert(text);
            if (!common_started) earlier_texts.insert(text);
            if (common_section) {
                if (character_count(text) < 2 || earlier_texts.count(text))
                    throw std::runtime_error("common expansion must contain new word texts" + context);
                if (!common_words.insert(text).second)
                    throw std::runtime_error("duplicate text in common expansion" + context);
            }
            ++rows;
        }
        check(!input.bad(), "dictionary audit could not finish reading");
        check(rows == lexicon.size(), "source rows were merged unexpectedly");
        check(computing_words.size() >= 2000, "computing expansion must contain at least 2000 unique words");
        check(daily_words.size() >= 5000, "daily expansion must contain at least 5000 unique words");
        check(common_words.size() >= 5000, "common expansion must contain at least 5000 new unique words");
        // GB2312 level 1 occupies B0A1-D7F9, with full 94-cell rows except
        // the final row. Windows code page 936 preserves this GB2312 range.
        // Decode the coverage baseline locally, without importing a word list.
        std::size_t level1_count = 0;
        for (int high = 0xb0; high <= 0xd7; ++high) {
            const int final_low = high == 0xd7 ? 0xf9 : 0xfe;
            for (int low = 0xa1; low <= final_low; ++low) {
                const char encoded[] = {static_cast<char>(high), static_cast<char>(low)};
                wchar_t character = 0; char utf8[4]{};
                if (MultiByteToWideChar(936, MB_ERR_INVALID_CHARS, encoded, 2, &character, 1) != 1)
                    throw std::runtime_error("cannot decode level-1 coverage baseline");
                const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, &character, 1, utf8, 4, nullptr, nullptr);
                if (!size) throw std::runtime_error("cannot encode level-1 coverage baseline");
                const std::string text(utf8, size);
                if (!single_readings.count(text)) throw std::runtime_error("missing level-1 character: " + text);
                ++level1_count;
            }
        }
        check(level1_count == 3755, "level-1 coverage range has changed");
        for (const auto& reading : readings) {
            // Every word must have independently usable characters for its
            // annotated pronunciation, so segmented selection cannot stall.
            std::istringstream syllables(reading.second);
            for (std::size_t start = 0; start < reading.first.size();) {
                auto end = start + 1;
                while (end < reading.first.size() && (static_cast<unsigned char>(reading.first[end]) & 0xc0) == 0x80) ++end;
                const auto text = reading.first.substr(start, end - start);
                std::string syllable; syllables >> syllable;
                const auto found = single_readings.find(text);
                if (found == single_readings.end() || !found->second.count(syllable))
                    throw std::runtime_error("word lacks a usable character reading: " + reading.first + " -> " + text + "/" + syllable);
                start = end;
            }
            const auto candidates = lexicon.lookup_composition(reading.second);
            if (std::none_of(candidates.begin(), candidates.end(), [&](const auto& candidate) {
                return candidate.text == reading.first && candidate.pronunciation == reading.second;
            })) throw std::runtime_error("reading is unreachable in the IME candidate list: " + reading.first + "/" + reading.second);
        }
        check(true, "character readings and candidate reachability audit completed");
        for (const auto& sample : samples)
            check(contains(lexicon, sample.query, sample.text, sample.pronunciation),
                  std::string("missing expected reading: ") + sample.query + " -> " + sample.text);
        for (const auto& sample : daily_samples)
            check(contains(lexicon, sample.query, sample.text, sample.pronunciation),
                  std::string("missing daily reading: ") + sample.query + " -> " + sample.text);
        for (const auto& sample : common_samples)
            check(contains(lexicon, sample.query, sample.text, sample.pronunciation),
                  std::string("missing common reading: ") + sample.query + " -> " + sample.text);
        for (const auto& sample : priorities) {
            const auto candidates = lexicon.lookup(sample.query);
            check(!candidates.empty() && candidates.front().text == sample.text &&
                      candidates.front().pronunciation == sample.pronunciation,
                  std::string("unexpected first candidate: ") + sample.query);
        }
        check(!contains(lexicon, "zhongqing", "重庆", "zhong qing"), "incorrect 重庆 reading");
        check(!contains(lexicon, "xingshu", "行数", "xing shu"), "incorrect 行数 reading");
        check(!contains(lexicon, "ren can", "人参", "ren can"), "incorrect 人参 reading");
        check(!contains(lexicon, "yin xing", "银行", "yin xing"), "incorrect 银行 reading");
        check(!contains(lexicon, "jiao se", "角色", "jiao se"), "incorrect 角色 reading");
        check(!contains(lexicon, "xuan", "癣", "xuan"), "incorrect 癣 reading");
        check(!contains(lexicon, "gui lie", "龟裂", "gui lie"), "incorrect 龟裂 reading");
        check(!contains(lexicon, "ju qiang", "倔强", "ju qiang"), "incorrect 倔强 reading");
        check(!contains(lexicon, "can cha", "参差", "can cha"), "incorrect 参差 reading");
        check(!contains(lexicon, "qia er bu she", "锲而不舍", "qia er bu she"), "incorrect 锲而不舍 reading");
        const Sample incorrect_computing[] = {
            {"huancunxing", "缓存行", "huan cun xing"},
            {"xianchengzusai", "线程阻塞", "xian cheng zu sai"},
            {"diaoshifuhao", "调试符号", "diao shi fu hao"},
            {"duozhongjicheng", "多重继承", "duo zhong ji cheng"},
            {"xingsuo", "行锁", "xing suo"},
            {"jiaosequanxian", "角色权限", "jiao se quan xian"},
            {"moxingweidiao", "模型微调", "mo xing wei diao"},
            {"guangzhahua", "光栅化", "guang zha hua"},
            {"gaojingfazhi", "告警阈值", "gao jing fa zhi"},
            {"maikuandiaozhi", "脉宽调制", "mai kuan diao zhi"},
        };
        for (const auto& sample : incorrect_computing)
            check(!contains(lexicon, sample.query, sample.text, sample.pronunciation),
                  std::string("incorrect computing reading: ") + sample.text);
        const Sample incorrect_daily[] = {
            {"niuziwaitao", "牛仔外套", "niu zi wai tao"},
            {"cha lv", "差旅", "cha lv"},
            {"bu jue", "补觉", "bu jue"},
            {"zhuanxing", "转行", "zhuan xing"},
            {"qile", "器乐", "qi le"},
            {"fengcheng", "丰盛", "feng cheng"},
            {"dian bing dang", "电饼铛", "dian bing dang"},
            {"mo bu", "抹布", "mo bu"},
            {"cuan qian", "攒钱", "cuan qian"},
            {"yi sha na", "一刹那", "yi sha na"},
        };
        for (const auto& sample : incorrect_daily)
            check(!contains(lexicon, sample.query, sample.text, sample.pronunciation),
                  std::string("incorrect daily reading: ") + sample.text);
        const Sample incorrect_common[] = {
            {"zuchang", "组长", "zu chang"},
            {"xuechang", "学长", "xue chang"},
            {"changye", "长叶", "chang ye"},
            {"dingtiao", "定调", "ding tiao"},
            {"jietiao", "借调", "jie tiao"},
            {"chaochaomumu", "朝朝暮暮", "chao chao mu mu"},
            {"koujiao", "口角", "kou jiao"},
            {"yongbuzhe", "用不着", "yong bu zhe"},
            {"fanbuzhe", "犯不着", "fan bu zhe"},
            {"xiaogao", "校稿", "xiao gao"},
            {"dajile", "打击乐", "da ji le"},
            {"xianle", "弦乐", "xian le"},
            {"gudianle", "古典乐", "gu dian le"},
            {"shana", "刹那", "sha na"},
            {"yueshikong", "钥匙孔", "yue shi kong"},
            {"baogenwendi", "刨根问底", "bao gen wen di"},
            {"wangbule", "忘不了", "wang bu le"},
            {"bunianguo", "不粘锅", "bu nian guo"},
            {"xinshizhongzhong", "心事重重", "xin shi zhong zhong"},
            {"shangxia", "商厦", "shang xia"},
            {"shidongfeidong", "似懂非懂", "shi dong fei dong"},
        };
        for (const auto& sample : incorrect_common)
            check(!contains(lexicon, sample.query, sample.text, sample.pronunciation),
                  std::string("incorrect common reading: ") + sample.text);
        pinyin::InputSession composition;
        for (const char letter : std::string("tingzhi")) composition.handle(pinyin::InputKey::letter, letter, lexicon);
        const auto stop = std::find_if(composition.candidates().begin(), composition.candidates().end(), [](const auto& word) { return word.text == "停"; });
        if (stop == composition.candidates().end()) throw std::runtime_error("missing 停 prefix for segmented regression");
        auto result = composition.select(static_cast<std::size_t>(stop - composition.candidates().begin()), lexicon);
        check(result.action == pinyin::InputAction::update && composition.preedit() == "停zhi", "selecting 停 lost the remaining zhi");
        const auto stagnate = std::find_if(composition.candidates().begin(), composition.candidates().end(), [](const auto& word) { return word.text == "滞"; });
        if (stagnate == composition.candidates().end()) throw std::runtime_error("missing 滞 for independent selection");
        result = composition.select(static_cast<std::size_t>(stagnate - composition.candidates().begin()), lexicon);
        check(result.action == pinyin::InputAction::commit && result.text == "停滞" && result.pronunciation == "ting zhi",
            "independent 滞 selection did not complete 停滞");
        std::cout << "PASS: audited " << rows << " rows, " << checks
                  << " checks; " << level1_count << " level-1 characters; load " << load_ms << " ms\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
