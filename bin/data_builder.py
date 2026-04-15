import re

def prepare_knowledge_base(input_file, output_file, max_lines=1500):
    print(f"正在读取原始文件: {input_file}...")
    try:
        with open(input_file, 'r', encoding='gb18030', errors='ignore') as f:
            raw_text = f.read()
    except Exception as e:
        print(f"读取失败，请检查文件是否存在或编码格式。错误: {e}")
        return

    # 核心：按句号、问号、感叹号、换行符进行“语义切片”
    print("正在进行语义切片清洗...")
    chunks = re.split(r'([。！？\n])', raw_text)

    sentences = []
    # 将标点符号拼回句子末尾
    for i in range(0, len(chunks)-1, 2):
        sentence = (chunks[i] + chunks[i+1]).strip()
        
        # 【数据清洗过滤规则】
        # 1. 过滤掉长度小于 10 个字的无意义短句（如“好。”、“谁？”）
        # 2. 过滤掉超过 200 个字的超长段落（大模型 Embedding 最喜欢 50-100 字的块）
        if 10 < len(sentence) < 200:
            # 去除句子中多余的空白符和换行
            sentence = re.sub(r'\s+', ' ', sentence)
            sentences.append(sentence)

    # 截取我们需要测试的数量
    final_sentences = sentences[:max_lines]

    print(f"正在生成目标知识库: {output_file}...")
    with open(output_file, 'w', encoding='utf-8') as f:
        for s in final_sentences:
            f.write(s + '\n')

    print(f"✅ 大功告成！成功提取了 {len(final_sentences)} 条高质量知识切片！")

# 执行切片，假设我们要 1500 条数据
prepare_knowledge_base('金庸-射雕英雄传txt精校版.txt', 'knowledge.txt', 1500)