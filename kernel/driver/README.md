ドライバは
{hoge_driver}/registers/registers.hppに考えられる生レジスタの操作をできるだけ薄くラップし、
{hoge_driver}/{hoge_driver}.hppに実際の外部からの操作を提供する。

HWレジスタのキャッシング、リソース管理以外の用途で、ステートを保持しないようにすること。
基本ステート管理は上位のレイヤで行うこと。
シングルトンの場合は、resource_managerを通じて管理すること。
